#pragma once

#include "foobar_sdk.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "riff_info.hpp"
#include "track_layout.hpp"

namespace cue_charset::foobar_component {

PFC_DECLARE_EXCEPTION(
    exception_cue_charset_input,
    exception_io,
    "CUE Charset Input error");

class CueDocument final {
public:
    static std::unique_ptr<CueDocument> load(
        file::ptr file_hint,
        const char* cue_path,
        abort_callback& abort);

    [[nodiscard]] const pfc::string8& cue_path() const noexcept;
    [[nodiscard]] const std::vector<detail::TrackSegment>& tracks() const noexcept;
    [[nodiscard]] const detail::TrackSegment& track(std::uint32_t number) const;
    [[nodiscard]] std::string diagnostic_context(
        std::optional<std::uint32_t> track_number = std::nullopt) const;

    void get_info(
        std::uint32_t track_number,
        file_info& info,
        abort_callback& abort) const;
    [[nodiscard]] t_filestats2 get_stats2(
        std::uint32_t flags,
        abort_callback& abort) const;

private:
    CueDocument() = default;

    void read_reference_info(
        const detail::TrackSegment& track,
        file_info& info,
        abort_callback& abort) const;
    [[nodiscard]] detail::RiffInfoUtf8Result riff_info_repair(
        const char* referenced_path,
        abort_callback& abort) const;
    static double track_length(
        const detail::TrackSegment& track,
        const file_info& reference_info);

    file::ptr cue_file_;
    pfc::string8 cue_path_;
    std::string cue_text_;
    std::string selection_diagnostic_;
    std::vector<detail::TrackSegment> tracks_;
    mutable std::mutex riff_info_mutex_;
    mutable std::unordered_map<std::string, detail::RiffInfoUtf8Result>
        riff_info_cache_;
};

[[noreturn]] void fail(const char* message);
[[noreturn]] void fail(const std::string& message);

} // namespace cue_charset::foobar_component
