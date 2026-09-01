#include "flac_duration_repair.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace cue_charset::detail {

std::optional<double> calculate_average_bitrate_kbps(
    const std::uint64_t file_size,
    const double duration_seconds) noexcept {
    if (file_size == 0 || !std::isfinite(duration_seconds) ||
        !(duration_seconds > 0)) {
        return std::nullopt;
    }

    const double bitrate =
        static_cast<double>(file_size) * 8.0 / duration_seconds / 1000.0;
    if (!std::isfinite(bitrate) || !(bitrate > 0)) {
        return std::nullopt;
    }
    return bitrate;
}

bool should_probe_flac_duration(
    const double declared_duration_seconds,
    const std::uint64_t file_size) noexcept {
    if (!std::isfinite(declared_duration_seconds) ||
        !(declared_duration_seconds > flac_suspicious_duration_seconds)) {
        return false;
    }
    const auto bitrate =
        calculate_average_bitrate_kbps(file_size, declared_duration_seconds);
    return bitrate.has_value() && *bitrate < flac_suspicious_bitrate_kbps;
}

bool FlacSampleAccumulator::add(
    const std::uint64_t sample_count,
    const std::uint32_t sample_rate) noexcept {
    if (status_ != FlacScanStatus::success) {
        return false;
    }
    if (sample_count == 0) {
        return true;
    }
    if (sample_rate == 0) {
        status_ = FlacScanStatus::invalid_sample_rate;
        return false;
    }
    if (sample_rate_ == 0) {
        sample_rate_ = sample_rate;
    } else if (sample_rate_ != sample_rate) {
        status_ = FlacScanStatus::sample_rate_changed;
        return false;
    }
    if (sample_count > std::numeric_limits<std::uint64_t>::max() - total_samples_) {
        status_ = FlacScanStatus::sample_count_overflow;
        return false;
    }
    total_samples_ += sample_count;
    return true;
}

FlacScanResult FlacSampleAccumulator::finish() const noexcept {
    if (status_ != FlacScanStatus::success) {
        return {status_, total_samples_, sample_rate_};
    }
    if (total_samples_ == 0 || sample_rate_ == 0) {
        return {FlacScanStatus::empty_output, total_samples_, sample_rate_};
    }
    return {FlacScanStatus::success, total_samples_, sample_rate_};
}

std::optional<FlacDurationCorrection> make_flac_duration_correction(
    const double declared_duration_seconds,
    const std::uint64_t file_size,
    const FlacScanResult& scan) noexcept {
    if (!std::isfinite(declared_duration_seconds) ||
        !(declared_duration_seconds > 0) ||
        scan.status != FlacScanStatus::success || scan.total_samples == 0 ||
        scan.sample_rate == 0) {
        return std::nullopt;
    }

    const double actual_duration =
        static_cast<double>(scan.total_samples) /
        static_cast<double>(scan.sample_rate);
    if (!std::isfinite(actual_duration) || !(actual_duration > 0) ||
        std::abs(actual_duration - declared_duration_seconds) <=
            flac_minimum_correction_delta_seconds) {
        return std::nullopt;
    }

    const auto bitrate = calculate_average_bitrate_kbps(file_size, actual_duration);
    if (!bitrate.has_value()) {
        return std::nullopt;
    }
    return FlacDurationCorrection{
        actual_duration,
        *bitrate,
        scan.total_samples,
        scan.sample_rate};
}

std::optional<FlacDurationCorrection> FlacDurationCorrectionCache::lookup(
    const std::string_view canonical_path,
    const FlacFileIdentity& identity) const {
    if (canonical_path.empty()) {
        return std::nullopt;
    }
    const std::scoped_lock lock(mutex_);
    const auto found = entries_.find(std::string(canonical_path));
    if (found == entries_.end() || found->second.identity != identity) {
        return std::nullopt;
    }
    return found->second.correction;
}

void FlacDurationCorrectionCache::store(
    std::string canonical_path,
    const FlacFileIdentity& identity,
    const FlacDurationCorrection& correction) {
    if (canonical_path.empty()) {
        return;
    }
    const std::scoped_lock lock(mutex_);
    entries_.insert_or_assign(
        std::move(canonical_path),
        Entry{identity, correction});
}

std::size_t FlacDurationCorrectionCache::size() const {
    const std::scoped_lock lock(mutex_);
    return entries_.size();
}

const char* flac_scan_status_name(const FlacScanStatus status) noexcept {
    switch (status) {
    case FlacScanStatus::success:
        return "success";
    case FlacScanStatus::empty_output:
        return "empty output";
    case FlacScanStatus::invalid_sample_rate:
        return "invalid sample rate";
    case FlacScanStatus::sample_rate_changed:
        return "sample rate changed";
    case FlacScanStatus::sample_count_overflow:
        return "sample count overflow";
    }
    return "unknown scan status";
}

} // namespace cue_charset::detail
