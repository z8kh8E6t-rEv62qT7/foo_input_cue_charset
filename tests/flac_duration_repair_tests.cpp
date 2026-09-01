#include "flac_duration_repair_tests.hpp"

#include "flac_duration_repair.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cue_charset::tests {
namespace {

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void require_close(
    const double actual,
    const double expected,
    const double tolerance,
    const std::string_view message) {
    require(std::abs(actual - expected) <= tolerance, message);
}

void test_probe_policy() {
    using detail::should_probe_flac_duration;

    require(
        should_probe_flac_duration(2871.826667, 9'567'386),
        "known malformed FLAC was not selected for probing");
    require(
        !should_probe_flac_duration(600.0, 1'000'000),
        "600-second boundary must not be probed");
    require(
        !should_probe_flac_duration(599.999, 1'000'000),
        "short FLAC was selected for probing");
    require(
        !should_probe_flac_duration(1000.0, 12'000'000),
        "96-kbps boundary must not be probed");
    require(
        should_probe_flac_duration(1000.0, 11'999'999),
        "bitrate immediately below 96 kbps was not probed");
    require(
        !should_probe_flac_duration(0.0, 1'000'000),
        "zero duration was selected for probing");
    require(
        !should_probe_flac_duration(
            std::numeric_limits<double>::quiet_NaN(),
            1'000'000),
        "NaN duration was selected for probing");
    require(
        !should_probe_flac_duration(
            std::numeric_limits<double>::infinity(),
            1'000'000),
        "infinite duration was selected for probing");
    require(
        !should_probe_flac_duration(1000.0, 0),
        "zero-size file was selected for probing");

    require(
        !detail::calculate_average_bitrate_kbps(0, 1.0).has_value(),
        "zero-size bitrate calculation succeeded");
    require(
        !detail::calculate_average_bitrate_kbps(1, 0.0).has_value(),
        "zero-duration bitrate calculation succeeded");
}

void test_sample_accumulator() {
    detail::FlacSampleAccumulator accumulator;
    require(accumulator.add(0, 0), "empty chunk should be ignored");
    require(accumulator.add(4'000, 44'100), "first chunk was rejected");
    require(accumulator.add(2'000, 44'100), "second chunk was rejected");
    const auto completed = accumulator.finish();
    require(
        completed.status == detail::FlacScanStatus::success,
        "valid scan did not complete successfully");
    require(completed.total_samples == 6'000, "sample total changed");
    require(completed.sample_rate == 44'100, "sample rate changed");

    detail::FlacSampleAccumulator empty;
    require(
        empty.finish().status == detail::FlacScanStatus::empty_output,
        "empty output was accepted");

    detail::FlacSampleAccumulator invalid_rate;
    require(!invalid_rate.add(1, 0), "invalid sample rate was accepted");
    require(
        invalid_rate.finish().status ==
            detail::FlacScanStatus::invalid_sample_rate,
        "invalid sample-rate status changed");

    detail::FlacSampleAccumulator changing_rate;
    require(changing_rate.add(1, 44'100), "initial rate was rejected");
    require(!changing_rate.add(1, 48'000), "sample-rate change was accepted");
    require(
        changing_rate.finish().status ==
            detail::FlacScanStatus::sample_rate_changed,
        "sample-rate change status changed");

    detail::FlacSampleAccumulator overflow;
    require(
        overflow.add(std::numeric_limits<std::uint64_t>::max(), 44'100),
        "maximum sample count was rejected prematurely");
    require(!overflow.add(1, 44'100), "sample-count overflow was accepted");
    require(
        overflow.finish().status ==
            detail::FlacScanStatus::sample_count_overflow,
        "sample-count overflow status changed");
}

void test_correction_calculation() {
    const detail::FlacScanResult target_scan{
        detail::FlacScanStatus::success,
        6'206'976,
        44'100};
    const auto target = detail::make_flac_duration_correction(
        2871.826667,
        9'567'386,
        target_scan);
    require(target.has_value(), "known malformed FLAC was not corrected");
    require_close(
        target->duration_seconds,
        140.747755102,
        0.000001,
        "corrected target duration changed");
    require_close(
        target->bitrate_kbps,
        543.803260,
        0.0001,
        "corrected target bitrate changed");

    const detail::FlacScanResult exact_delta{
        detail::FlacScanStatus::success,
        900,
        100};
    require(
        !detail::make_flac_duration_correction(10.0, 1'000, exact_delta)
             .has_value(),
        "exactly one-second difference was corrected");

    const detail::FlacScanResult larger_delta{
        detail::FlacScanStatus::success,
        899,
        100};
    require(
        detail::make_flac_duration_correction(10.0, 1'000, larger_delta)
            .has_value(),
        "difference greater than one second was not corrected");

    const detail::FlacScanResult failed{
        detail::FlacScanStatus::sample_rate_changed,
        100,
        44'100};
    require(
        !detail::make_flac_duration_correction(1000.0, 1'000, failed)
             .has_value(),
        "failed scan produced a correction");
}

void test_correction_cache() {
    detail::FlacDurationCorrectionCache cache;
    const detail::FlacFileIdentity original{9'567'386, 123};
    const detail::FlacFileIdentity changed_size{9'567'387, 123};
    const detail::FlacFileIdentity changed_time{9'567'386, 124};
    const detail::FlacDurationCorrection correction{
        140.747755102,
        543.802650,
        6'206'976,
        44'100};

    require(
        !cache.lookup("file://I:/disc/track.flac", original).has_value(),
        "new cache unexpectedly contained a result");
    cache.store("file://I:/disc/track.flac", original, correction);
    require(cache.size() == 1, "cache size changed after insertion");

    const auto direct = cache.lookup("file://I:/disc/track.flac", original);
    require(direct.has_value(), "direct FLAC cache lookup missed");
    const auto cue_reference =
        cache.lookup("file://I:/disc/track.flac", original);
    require(
        cue_reference.has_value(),
        "CUE-referenced lookup did not share the direct FLAC result");
    require_close(
        cue_reference->duration_seconds,
        correction.duration_seconds,
        0.0,
        "cached correction changed");

    require(
        !cache.lookup("file://I:/disc/track.flac", changed_size).has_value(),
        "file-size change did not invalidate cache entry");
    require(
        !cache.lookup("file://I:/disc/track.flac", changed_time).has_value(),
        "modification-time change did not invalidate cache entry");
    require(
        !cache.lookup("file://I:/disc/other.flac", original).has_value(),
        "different path reused cache entry");

    cache.store("file://I:/disc/track.flac", changed_time, correction);
    require(cache.size() == 1, "changed file left a stale cache entry");
    require(
        cache.lookup("file://I:/disc/track.flac", changed_time).has_value(),
        "changed file identity was not stored");
    require(
        !cache.lookup("", changed_time).has_value(),
        "empty cache key was accepted");
}

} // namespace

void run_flac_duration_repair_tests() {
    test_probe_policy();
    test_sample_accumulator();
    test_correction_calculation();
    test_correction_cache();
}

} // namespace cue_charset::tests
