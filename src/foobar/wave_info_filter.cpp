#include "wave_info_filter.hpp"

#include <cstddef>
#include <exception>

#include "charset_analyzer.hpp"
#include "component_diagnostics.hpp"
#include "riff_info_repair.hpp"

namespace cue_charset::foobar_component {
namespace {

thread_local std::size_t g_bypass_depth = 0;

bool is_direct_local_wave(const playable_location& location) {
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
    return pfc::stricmp_ascii(extension.c_str(), "wav") == 0 ||
           pfc::stricmp_ascii(extension.c_str(), "wave") == 0;
}

class WaveRiffInfoFilter : public input_info_filter {
public:
    void filter_info_read(
        const playable_location& location,
        file_info& info,
        abort_callback& abort) override {
        if (wave_info_filter_is_bypassed() || !is_direct_local_wave(location)) {
            return;
        }

        const char* const path = location.get_path();
        try {
            const auto repair = load_riff_info_utf8_repair(
                path,
                shared_charset_analyzer(),
                abort);
            apply_riff_info_utf8_repair(repair, info);
            if (repair.status == detail::RiffInfoUtf8Status::invalid_text) {
                log_warning(
                    "wave-filter",
                    "skipped",
                    (PFC_string_formatter()
                     << "path=" << path
                     << ", mode=" << detail::riff_info_read_mode_name(repair.mode)
                     << ", reason=" << repair.reason.c_str())
                        .c_str());
            }
        } catch (const exception_aborted&) {
            throw;
        } catch (const std::exception& error) {
            log_warning(
                "wave-filter",
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
        return wave_riff_info_filter_guid;
    }

    GUID get_preferences_guid() override {
        return preferences_page::guid_input_info_filter;
    }

    const char* get_name() override {
        return "CUE Charset RIFF INFO Filter";
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

service_factory_single_t<WaveRiffInfoFilter> g_wave_riff_info_filter_factory;

} // namespace

ScopedWaveInfoFilterBypass::ScopedWaveInfoFilterBypass() noexcept {
    ++g_bypass_depth;
}

ScopedWaveInfoFilterBypass::~ScopedWaveInfoFilterBypass() {
    PFC_ASSERT(g_bypass_depth > 0);
    --g_bypass_depth;
}

bool wave_info_filter_is_bypassed() noexcept {
    return g_bypass_depth != 0;
}

} // namespace cue_charset::foobar_component
