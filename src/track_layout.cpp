#include "track_layout.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <string_view>

#include "cue_charset/cue_charset.hpp"

namespace cue_charset::detail {

std::vector<TrackSegment> build_track_layout(
    const std::span<const TrackDescriptor> tracks) {
    if (tracks.empty()) {
        throw Error("CUE sheet contains no audio tracks");
    }

    std::vector<TrackSegment> result;
    result.reserve(tracks.size());

    std::uint32_t previous_number = 0;
    for (std::size_t index = 0; index < tracks.size(); ++index) {
        const auto& track = tracks[index];
        if (track.number == 0 || track.number <= previous_number) {
            throw Error("CUE audio track numbers are not strictly increasing");
        }
        if (track.referenced_path.empty()) {
            throw Error("CUE audio track has an empty referenced path");
        }
        if (!std::isfinite(track.start_seconds) || track.start_seconds < 0) {
            throw Error("CUE audio track has an invalid INDEX 01 position");
        }

        std::optional<double> known_length;
        if (index + 1 < tracks.size()) {
            const auto& next = tracks[index + 1];
            if (track.referenced_path == next.referenced_path) {
                const double length = next.start_seconds - track.start_seconds;
                if (!std::isfinite(length) || length <= 0) {
                    throw Error("adjacent CUE tracks in one file have invalid INDEX 01 order");
                }
                known_length = length;
            }
        }

        result.push_back(TrackSegment{
            track.number,
            track.referenced_path,
            track.binary,
            track.start_seconds,
            known_length});
        previous_number = track.number;
    }
    return result;
}

const TrackSegment* find_track(
    const std::span<const TrackSegment> tracks,
    const std::uint32_t number) noexcept {
    const auto found = std::find_if(
        tracks.begin(),
        tracks.end(),
        [number](const TrackSegment& track) { return track.number == number; });
    return found == tracks.end() ? nullptr : std::addressof(*found);
}

CueSubsongLocation make_cue_subsong_location(
    const std::string_view canonical_cue_path,
    const TrackSegment& track) {
    if (canonical_cue_path.empty()) {
        throw Error("canonical CUE path is empty");
    }
    if (track.number == 0) {
        throw Error("CUE subsong must use a positive TRACK number");
    }
    return CueSubsongLocation{std::string(canonical_cue_path), track.number};
}

} // namespace cue_charset::detail
