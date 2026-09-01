#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace cue_charset::detail {

inline constexpr double flac_suspicious_duration_seconds = 600.0;
inline constexpr double flac_suspicious_bitrate_kbps = 96.0;
inline constexpr double flac_minimum_correction_delta_seconds = 1.0;

[[nodiscard]] std::optional<double> calculate_average_bitrate_kbps(
    std::uint64_t file_size,
    double duration_seconds) noexcept;

[[nodiscard]] bool should_probe_flac_duration(
    double declared_duration_seconds,
    std::uint64_t file_size) noexcept;

enum class FlacScanStatus {
    success,
    empty_output,
    invalid_sample_rate,
    sample_rate_changed,
    sample_count_overflow,
};

struct FlacScanResult {
    FlacScanStatus status = FlacScanStatus::empty_output;
    std::uint64_t total_samples = 0;
    std::uint32_t sample_rate = 0;
};

class FlacSampleAccumulator final {
public:
    [[nodiscard]] bool add(
        std::uint64_t sample_count,
        std::uint32_t sample_rate) noexcept;

    [[nodiscard]] FlacScanResult finish() const noexcept;

private:
    FlacScanStatus status_ = FlacScanStatus::success;
    std::uint64_t total_samples_ = 0;
    std::uint32_t sample_rate_ = 0;
};

struct FlacDurationCorrection {
    double duration_seconds = 0;
    double bitrate_kbps = 0;
    std::uint64_t total_samples = 0;
    std::uint32_t sample_rate = 0;
};

[[nodiscard]] std::optional<FlacDurationCorrection>
make_flac_duration_correction(
    double declared_duration_seconds,
    std::uint64_t file_size,
    const FlacScanResult& scan) noexcept;

struct FlacFileIdentity {
    std::uint64_t file_size = 0;
    std::uint64_t modification_time = 0;

    friend bool operator==(
        const FlacFileIdentity& left,
        const FlacFileIdentity& right) noexcept = default;
};

class FlacDurationCorrectionCache final {
public:
    [[nodiscard]] std::optional<FlacDurationCorrection> lookup(
        std::string_view canonical_path,
        const FlacFileIdentity& identity) const;

    void store(
        std::string canonical_path,
        const FlacFileIdentity& identity,
        const FlacDurationCorrection& correction);

    [[nodiscard]] std::size_t size() const;

private:
    struct Entry {
        FlacFileIdentity identity;
        FlacDurationCorrection correction;
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::string, Entry> entries_;
};

[[nodiscard]] const char* flac_scan_status_name(FlacScanStatus status) noexcept;

} // namespace cue_charset::detail
