#include "cue_document.hpp"

#include <foobar2000/helpers/cue_parser.h>
#include <foobar2000/helpers/input_helper_cue.h>
#include <foobar2000/helpers/input_helpers.h>

#include <array>
#include <cstddef>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "component_diagnostics.hpp"
#include "charset_analyzer.hpp"
#include "cue_reference_path.hpp"
#include "cue_charset/cue_charset.hpp"
#include "riff_info_repair.hpp"
#include "wave_info_filter.hpp"

namespace cue_charset::foobar_component {
namespace {

template <typename Function>
decltype(auto) diagnostic_stage(const char* stage, Function&& function) {
    try {
        return std::forward<Function>(function)();
    } catch (const exception_aborted&) {
        throw;
    } catch (const std::exception& error) {
        log_failure("load", stage, error);
        const pfc::string8 diagnostic =
            PFC_string_formatter()
            << "CUE Charset Input [" << stage << "]: " << error.what();
        pfc::throw_exception_with_message<exception_cue_charset_input>(diagnostic);
    }
}

std::vector<std::byte> read_cue_file(file::ptr source, abort_callback& abort) {
    source->reopen(abort);
    const t_filesize reported_size = source->get_size(abort);
    if (reported_size != filesize_invalid) {
        if (reported_size == 0) {
            fail("CUE file is empty");
        }
        if (reported_size > max_file_size) {
            fail("CUE file exceeds the 128 MiB limit");
        }
        if (reported_size > static_cast<t_filesize>(std::numeric_limits<std::size_t>::max())) {
            fail("CUE file is too large for this process");
        }
        std::vector<std::byte> bytes(static_cast<std::size_t>(reported_size));
        source->read_object(bytes.data(), bytes.size(), abort);
        return bytes;
    }

    constexpr std::size_t chunk_size = 64U * 1024U;
    std::array<std::byte, chunk_size> chunk{};
    std::vector<std::byte> bytes;
    for (;;) {
        const std::size_t count = source->read(chunk.data(), chunk.size(), abort);
        if (count == 0) {
            break;
        }
        if (bytes.size() > max_file_size - count) {
            fail("CUE file exceeds the 128 MiB limit");
        }
        bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + count);
    }
    if (bytes.empty()) {
        fail("CUE file is empty");
    }
    return bytes;
}

std::string convert_cue(
    const Analyzer& analyzer,
    const std::span<const std::byte> bytes,
    const DetectionResult& detection) {
    auto converted = analyzer.convert(bytes, detection.encoding).utf8;
    constexpr std::string_view utf8_bom{"\xEF\xBB\xBF", 3};
    if (converted.starts_with(utf8_bom)) {
        converted.erase(0, utf8_bom.size());
    }
    return converted;
}

bool resolve_cue_reference(
    const char* reference,
    const char* cue_path,
    pfc::string_base& resolved) {
    const auto kind = detail::classify_cue_reference(reference);
    if (kind == detail::CueReferenceKind::drive_absolute ||
        kind == detail::CueReferenceKind::unc_absolute) {
        filesystem::g_get_canonical_path(reference, resolved);
        return true;
    }
    if (kind != detail::CueReferenceKind::relative) {
        return false;
    }
    if (filesystem::g_relative_path_parse(reference, cue_path, resolved)) {
        return true;
    }

    pfc::string8 native_cue_path;
    if (!foobar2000_io::extract_native_path(cue_path, native_cue_path)) {
        return false;
    }
    const pfc::string parent = pfc::io::path::getParent(native_cue_path.c_str());
    if (parent.isEmpty()) {
        return false;
    }
    const pfc::string combined = pfc::io::path::combine(parent, reference);
    filesystem::g_get_canonical_path(combined.c_str(), resolved);
    return true;
}

std::vector<detail::TrackSegment> parse_tracks(
    const std::string& cue_text,
    const char* cue_path) {
    cue_parser::t_cue_entry_list parsed;
    try {
        cue_parser::parse(cue_text.c_str(), parsed);
    } catch (const cue_parser::exception_bad_cuesheet& error) {
        fail(error.what());
    }

    std::vector<detail::TrackDescriptor> descriptors;
    descriptors.reserve(parsed.get_count());
    for (const auto& entry : parsed) {
        pfc::string8 resolved;
        if (!resolve_cue_reference(entry.m_file.c_str(), cue_path, resolved)) {
            fail(
                std::string("cannot resolve referenced file: ") + entry.m_file.c_str() +
                " (CUE base: " + cue_path + ")");
        }
        descriptors.push_back(detail::TrackDescriptor{
            entry.m_track_number,
            resolved.c_str(),
            entry.isFileBinary(),
            entry.m_indexes.start()});
    }
    try {
        return detail::build_track_layout(descriptors);
    } catch (const Error& error) {
        fail(error.what());
    }
}

bool referenced_files_exist(
    const std::vector<detail::TrackSegment>& tracks,
    abort_callback& abort) {
    for (const auto& track : tracks) {
        abort.check();
        if (!filesystem::g_exists(track.referenced_path.c_str(), abort)) {
            return false;
        }
    }
    return true;
}

struct SelectedCueCandidate {
    std::string text;
    std::vector<detail::TrackSegment> tracks;
};

std::optional<SelectedCueCandidate> select_usable_statistical_candidate(
    const Analyzer& analyzer,
    const std::span<const std::byte> bytes,
    const std::vector<DetectionResult>& candidates,
    const char* cue_path,
    abort_callback& abort) {
    if (candidates.empty() || candidates.front().unicode_signature.has_value()) {
        return std::nullopt;
    }

    for (std::size_t index = 0; index < candidates.size(); ++index) {
        abort.check();

        ConversionResult conversion;
        try {
            conversion = analyzer.convert(bytes, candidates[index].encoding);
        } catch (const Error&) {
            continue;
        }
        if (conversion.replacement_count != 0) {
            continue;
        }

        std::vector<detail::TrackSegment> tracks;
        try {
            tracks = parse_tracks(conversion.utf8, cue_path);
        } catch (const exception_cue_charset_input&) {
            continue;
        }
        if (!referenced_files_exist(tracks, abort)) {
            continue;
        }

        if (index != 0) {
            const pfc::string8 details =
                PFC_string_formatter()
                << "selected " << candidates[index].encoding.c_str()
                << " (ICU confidence " << *candidates[index].confidence
                << ") after " << candidates.front().encoding.c_str()
                << " failed strict conversion or referenced-file validation";
            log_warning("load", "charset-candidate-fallback", details.c_str());
        }
        return SelectedCueCandidate{
            std::move(conversion.utf8), std::move(tracks)};
    }
    return std::nullopt;
}

} // namespace

[[noreturn]] void fail(const char* message) {
    pfc::throw_exception_with_message<exception_cue_charset_input>(
        PFC_string_formatter() << "CUE Charset Input: " << message);
}

[[noreturn]] void fail(const std::string& message) {
    fail(message.c_str());
}

std::unique_ptr<CueDocument> CueDocument::load(
    file::ptr file_hint,
    const char* cue_path,
    abort_callback& abort) {
    auto result = std::unique_ptr<CueDocument>(new CueDocument());
    diagnostic_stage("file-open", [&] {
        result->cue_file_ = std::move(file_hint);
        if (result->cue_file_.is_empty()) {
            filesystem::g_open_read(result->cue_file_, cue_path, abort);
        } else {
            result->cue_file_->reopen(abort);
        }
        filesystem::g_get_canonical_path(cue_path, result->cue_path_);
    });
    const auto bytes = diagnostic_stage(
        "file-read", [&] { return read_cue_file(result->cue_file_, abort); });
    const auto& analyzer = diagnostic_stage(
        "icu-load", []() -> const Analyzer& { return shared_charset_analyzer(); });
    const auto candidates = diagnostic_stage(
        "charset-detection", [&] { return analyzer.detect_candidates(bytes); });
    if (auto selected = select_usable_statistical_candidate(
            analyzer,
            bytes,
            candidates,
            result->cue_path_.c_str(),
            abort)) {
        result->cue_text_ = std::move(selected->text);
        result->tracks_ = std::move(selected->tracks);
        return result;
    }

    result->cue_text_ = diagnostic_stage(
        "utf8-conversion", [&] { return convert_cue(analyzer, bytes, candidates.front()); });
    result->tracks_ = diagnostic_stage(
        "cue-parse-path-resolution",
        [&] { return parse_tracks(result->cue_text_, result->cue_path_.c_str()); });
    return result;
}

const pfc::string8& CueDocument::cue_path() const noexcept {
    return cue_path_;
}

const std::vector<detail::TrackSegment>& CueDocument::tracks() const noexcept {
    return tracks_;
}

const detail::TrackSegment& CueDocument::track(const std::uint32_t number) const {
    const auto* found = detail::find_track(tracks_, number);
    if (found == nullptr) {
        throw exception_io_bad_subsong_index();
    }
    return *found;
}

void CueDocument::get_info(
    const std::uint32_t track_number,
    file_info& info,
    abort_callback& abort) const {
    const auto& selected = track(track_number);
    read_reference_info(selected, info, abort);
    try {
        cue_parser::parse_info(cue_text_.c_str(), info, selected.number);
    } catch (const cue_parser::exception_bad_cuesheet& error) {
        fail(error.what());
    }
    info.set_length(track_length(selected, info));
    info.info_set("CUE_SOURCE_PATH", cue_path_.c_str());
    info.info_set("REFERENCED_FILE", selected.referenced_path.c_str());
}

t_filestats2 CueDocument::get_stats2(
    const std::uint32_t flags,
    abort_callback& abort) const {
    return cue_file_->get_stats2_(flags, abort);
}

void CueDocument::read_reference_info(
    const detail::TrackSegment& selected,
    file_info& info,
    abort_callback& abort) const {
    info.reset();
    if (selected.binary) {
        input_helper_cue::get_info_binary(selected.referenced_path.c_str(), info, abort);
    } else {
        {
            const ScopedWaveInfoFilterBypass filter_bypass;
            input_helper::g_get_info(
                make_playable_location(selected.referenced_path.c_str(), 0),
                info,
                abort,
                true);
        }
        apply_riff_info_utf8_repair(
            riff_info_repair(selected.referenced_path.c_str(), abort),
            info);
    }
}

detail::RiffInfoUtf8Result CueDocument::riff_info_repair(
    const char* const referenced_path,
    abort_callback& abort) const {
    const std::string cache_key(referenced_path);
    std::scoped_lock lock(riff_info_mutex_);
    if (const auto found = riff_info_cache_.find(cache_key);
        found != riff_info_cache_.end()) {
        return found->second;
    }

    detail::RiffInfoUtf8Result result;
    try {
        result = load_riff_info_utf8_repair(
            referenced_path,
            shared_charset_analyzer(),
            abort);
    } catch (const exception_aborted&) {
        throw;
    } catch (const std::exception& error) {
        result.status = detail::RiffInfoUtf8Status::invalid_text;
        result.reason = error.what();
    }

    if (result.status == detail::RiffInfoUtf8Status::invalid_text) {
        log_warning(
            "riff-info-utf8",
            "skipped",
            (PFC_string_formatter()
             << "cue=" << cue_path_.c_str()
             << ", referenced=" << referenced_path
             << ", mode=" << detail::riff_info_read_mode_name(result.mode)
             << ", reason=" << result.reason.c_str())
                .c_str());
    }

    riff_info_cache_.emplace(cache_key, result);
    return result;
}

double CueDocument::track_length(
    const detail::TrackSegment& selected,
    const file_info& reference_info) {
    if (selected.known_length_seconds.has_value()) {
        return *selected.known_length_seconds;
    }
    const double length = reference_info.get_length() - selected.start_seconds;
    if (!(length > 0)) {
        fail("referenced audio length is unknown or does not cover the CUE track");
    }
    return length;
}

} // namespace cue_charset::foobar_component
