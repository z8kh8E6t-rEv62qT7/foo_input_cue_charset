#include "flac_duration_filter.hpp"

#include <cmath>
#include <cstdint>
#include <exception>

#include "component_diagnostics.hpp"
#include "flac_duration_repair.hpp"

namespace cue_charset::foobar_component {
namespace {

detail::FlacDurationCorrectionCache g_correction_cache;

bool is_direct_local_flac(const playable_location& location) {
    if (location.get_subsong_index() != 0) {
        return false;
    }

    const char* const path = location.get_path();
    if (path == nullptr || *path == '\0') {
        return false;
    }
    pfc::string8 native_path;
    if (!foobar2000_io::extract_native_path(path, native_path)) {
        return false;
    }

    const pfc::string8 extension = filesystem::g_get_extension(path);
    return pfc::stricmp_ascii(extension.c_str(), "flac") == 0;
}

void apply_correction(
    const detail::FlacDurationCorrection& correction,
    const std::uint64_t file_size,
    file_info& info) {
    info.set_length(correction.duration_seconds);
    info.info_calculate_bitrate(file_size, correction.duration_seconds);
}

class FlacDurationRepairFilter : public input_info_filter {
public:
    void filter_info_read(
        const playable_location& location,
        file_info& info,
        abort_callback& abort) override {
        if (!is_direct_local_flac(location)) {
            return;
        }

        const char* const path = location.get_path();
        try {
            const auto stats = filesystem::g_get_stats2(
                path,
                stats2_size | stats2_timestamp,
                abort);
            if (!stats.haveSize() || stats.m_size == 0 ||
                !stats.haveTimestamp()) {
                log_warning(
                    "flac-duration-filter",
                    "skipped",
                    (PFC_string_formatter()
                     << "path=" << path
                     << ", reason=invalid file size or modification time")
                        .c_str());
                return;
            }

            pfc::string8 canonical_path;
            filesystem::g_get_canonical_path(path, canonical_path);
            const detail::FlacFileIdentity identity{
                stats.m_size,
                stats.m_timestamp};

            if (const auto cached =
                    g_correction_cache.lookup(canonical_path.c_str(), identity);
                cached.has_value()) {
                apply_correction(*cached, stats.m_size, info);
                return;
            }

            const double declared_duration = info.get_length();
            if (!std::isfinite(declared_duration) || !(declared_duration > 0)) {
                log_warning(
                    "flac-duration-filter",
                    "skipped",
                    (PFC_string_formatter()
                     << "path=" << path
                     << ", reason=invalid declared duration")
                        .c_str());
                return;
            }
            if (!detail::should_probe_flac_duration(
                    declared_duration,
                    stats.m_size)) {
                return;
            }

            input_decoder::ptr decoder;
            input_entry::g_open_for_decoding(decoder, nullptr, path, abort);
            decoder->initialize(
                0,
                input_flag_simpledecode | input_flag_no_postproc,
                abort);

            detail::FlacSampleAccumulator accumulator;
            audio_chunk_impl chunk;
            while (decoder->run(chunk, abort)) {
                if (!accumulator.add(
                        static_cast<std::uint64_t>(chunk.get_sample_count()),
                        static_cast<std::uint32_t>(chunk.get_sample_rate()))) {
                    break;
                }
            }

            const auto scan = accumulator.finish();
            if (scan.status != detail::FlacScanStatus::success) {
                log_warning(
                    "flac-duration-filter",
                    "skipped",
                    (PFC_string_formatter()
                     << "path=" << path
                     << ", reason=" << detail::flac_scan_status_name(scan.status))
                        .c_str());
                return;
            }

            const auto correction = detail::make_flac_duration_correction(
                declared_duration,
                stats.m_size,
                scan);
            if (!correction.has_value()) {
                return;
            }

            apply_correction(*correction, stats.m_size, info);
            g_correction_cache.store(
                canonical_path.c_str(),
                identity,
                *correction);
            log_warning(
                "flac-duration-filter",
                "corrected",
                (PFC_string_formatter()
                 << "path=" << path
                 << ", declared="
                 << pfc::format_float(declared_duration, 0, 3)
                 << " s, actual="
                 << pfc::format_float(correction->duration_seconds, 0, 3)
                 << " s, bitrate="
                 << pfc::format_float(correction->bitrate_kbps, 0, 1)
                 << " kbps")
                    .c_str());
        } catch (const exception_aborted&) {
            throw;
        } catch (const std::exception& error) {
            log_warning(
                "flac-duration-filter",
                "skipped",
                (PFC_string_formatter()
                 << "path=" << path
                 << ", reason=" << error.what())
                    .c_str());
        }
    }

    bool filter_info_write(
        const playable_location&,
        file_info&,
        abort_callback&) override {
        return true;
    }

    void on_info_remove(const char*, abort_callback&) override {}

    GUID get_guid() override {
        return flac_duration_repair_filter_guid;
    }

    GUID get_preferences_guid() override {
        return preferences_page::guid_input_info_filter;
    }

    const char* get_name() override {
        return "CUE Charset FLAC Duration Repair";
    }

    bool supports_fallback() override {
        return false;
    }

    bool write_fallback(
        const playable_location&,
        const file_info&,
        abort_callback&) override {
        return false;
    }

    void remove_tags_fallback(const char*, abort_callback&) override {}
};

service_factory_single_t<FlacDurationRepairFilter>
    g_flac_duration_repair_filter_factory;

} // namespace
} // namespace cue_charset::foobar_component
