#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace cue_charset::detail {

struct TrackDescriptor {
    std::uint32_t number = 0;
    std::string referenced_path;
    bool binary = false;
    double start_seconds = 0;
};

struct TrackSegment {
    std::uint32_t number = 0;
    std::string referenced_path;
    bool binary = false;
    double start_seconds = 0;
    std::optional<double> known_length_seconds;
};

struct CueSubsongLocation {
    std::string cue_path;
    std::uint32_t subsong = 0;
};

[[nodiscard]] std::vector<TrackSegment> build_track_layout(
    std::span<const TrackDescriptor> tracks);

[[nodiscard]] const TrackSegment* find_track(
    std::span<const TrackSegment> tracks,
    std::uint32_t number) noexcept;

[[nodiscard]] CueSubsongLocation make_cue_subsong_location(
    std::string_view canonical_cue_path,
    const TrackSegment& track);

} // namespace cue_charset::detail
