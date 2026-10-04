#include "cue_charset/cue_charset.hpp"
#include "cue_reference_path.hpp"
#include "cue_encoding_tests.hpp"
#include "flac_duration_repair_tests.hpp"
#include "track_layout.hpp"
#include "riff_info_tests.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

class TestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw TestFailure(std::string(message));
    }
}

template <typename Callable>
void require_error(Callable&& callable, const std::string_view expected_fragment) {
    try {
        std::invoke(std::forward<Callable>(callable));
    } catch (const cue_charset::Error& error) {
        require(
            std::string_view(error.what()).find(expected_fragment) != std::string_view::npos,
            std::string("unexpected error text: ") + error.what());
        return;
    }
    throw TestFailure("expected cue_charset::Error was not thrown");
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    require(static_cast<bool>(stream), "unable to read expected UTF-8 fixture");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::vector<std::byte> read_bytes(const std::filesystem::path& path) {
    const auto text = read_text(path);
    const auto* begin = reinterpret_cast<const std::byte*>(text.data());
    return {begin, begin + text.size()};
}

std::vector<std::byte> read_hex_bytes(const std::filesystem::path& path) {
    const auto hex = read_text(path);
    std::vector<std::byte> bytes;
    int high_nibble = -1;
    for (const char value : hex) {
        int nibble = -1;
        if (value >= '0' && value <= '9') {
            nibble = value - '0';
        } else if (value >= 'a' && value <= 'f') {
            nibble = value - 'a' + 10;
        } else if (value >= 'A' && value <= 'F') {
            nibble = value - 'A' + 10;
        } else if (value == ' ' || value == '\t' || value == '\r' || value == '\n') {
            continue;
        } else {
            throw TestFailure("hex fixture contains a non-hex character");
        }

        if (high_nibble < 0) {
            high_nibble = nibble;
        } else {
            bytes.push_back(static_cast<std::byte>((high_nibble << 4) | nibble));
            high_nibble = -1;
        }
    }
    require(high_nibble < 0, "hex fixture contains an incomplete byte");
    require(!bytes.empty(), "hex fixture is empty");
    return bytes;
}

void test_gb18030_end_to_end(
    const cue_charset::Analyzer& analyzer,
    const std::filesystem::path& fixtures) {
    const auto result = analyzer.analyze_file(fixtures / "gb18030.cue");
    require(result.detection.encoding == "GB18030", "ICU first candidate was not GB18030");
    require(!result.detection.unicode_signature.has_value(), "GB18030 fixture has a signature");
    require(result.detection.confidence.has_value(), "statistical result has no confidence");
    require(result.conversion.replacement_count == 0, "valid GB18030 fixture had replacements");
    require(
        result.conversion.utf8 == read_text(fixtures / "expected-utf8.txt"),
        "converted Unicode text did not match the canonical UTF-8 fixture");

    const auto diagnostics = cue_charset::format_diagnostics(result);
    require(diagnostics.find("unicode_signature=none") != std::string::npos, "missing signature diagnostic");
    require(diagnostics.find("encoding=GB18030") != std::string::npos, "missing encoding diagnostic");
    require(diagnostics.find("conversion=success") != std::string::npos, "missing conversion diagnostic");
    require(diagnostics.find("first_replacement_offset=N/A") != std::string::npos, "missing replacement diagnostic");
}

void test_corrupt_gb18030(
    const cue_charset::Analyzer& analyzer,
    const std::filesystem::path& fixtures) {
    const auto bytes = read_bytes(fixtures / "gb18030-corrupt.cue");
    const auto result = analyzer.convert(bytes, "GB18030");
    require(result.replacement_count == 1, "corrupt fixture did not produce one replacement");
    require(result.first_replacement_offset.has_value(), "corrupt fixture has no problem offset");
    require(*result.first_replacement_offset == 19, "corrupt fixture problem offset changed");
    require(result.utf8.find("\xEF\xBF\xBD") != std::string::npos, "UTF-8 result lacks U+FFFD");
}

void test_unicode_signature(const cue_charset::Analyzer& analyzer) {
    constexpr std::array bytes{
        std::byte{0xEF}, std::byte{0xBB}, std::byte{0xBF}, std::byte{'A'}};
    const auto detection = analyzer.detect(bytes);
    require(detection.unicode_signature == "UTF-8", "UTF-8 signature was not detected");
    require(detection.signature_length == 3, "UTF-8 signature length was not three");
    require(detection.encoding == "UTF-8", "signature did not determine the encoding");
    require(!detection.confidence.has_value(), "signature result unexpectedly has confidence");

    const auto candidates = analyzer.detect_candidates(bytes);
    require(candidates.size() == 1, "signature detection returned statistical candidates");
    require(candidates.front().encoding == "UTF-8", "signature candidate changed");
}

void test_statistical_candidates(
    const cue_charset::Analyzer& analyzer,
    const std::filesystem::path& fixtures) {
    const auto ordinary = read_bytes(fixtures / "gb18030.cue");
    const auto ordinary_candidates = analyzer.detect_candidates(ordinary);
    require(!ordinary_candidates.empty(), "statistical detection returned no candidates");
    require(
        ordinary_candidates.front().encoding == analyzer.detect(ordinary).encoding,
        "detect() no longer returns the first ordered candidate");

    const auto misdetected =
        read_hex_bytes(fixtures / "gb18030-big5-misdetection.hex");
    const auto candidates = analyzer.detect_candidates(misdetected);
    require(candidates.size() >= 2, "misdetection fixture returned fewer than two candidates");
    require(
        candidates.front().encoding == "Big5",
        std::string("ICU no longer ranks Big5 first for fixture; first was ") +
            candidates.front().encoding);

    const auto gb18030 = std::find_if(
        candidates.begin(), candidates.end(), [](const cue_charset::DetectionResult& value) {
            return value.encoding == "GB18030";
        });
    require(gb18030 != candidates.end(), "GB18030 alternative candidate is missing");
    require(gb18030 != candidates.begin(), "GB18030 unexpectedly became the first candidate");

    const auto converted = analyzer.convert(misdetected, gb18030->encoding);
    require(converted.replacement_count == 0, "GB18030 candidate required replacements");
    require(
        converted.utf8.find("夏影") != std::string::npos,
        "GB18030 candidate did not recover the expected path text");
}

void test_file_errors(
    const cue_charset::Analyzer& analyzer,
    const std::filesystem::path& temporary) {
    std::filesystem::create_directories(temporary);
    const auto empty = temporary / "empty.cue";
    { std::ofstream stream(empty, std::ios::binary); }
    require_error([&] { (void)analyzer.analyze_file(empty); }, "empty");

    require_error(
        [&] { (void)analyzer.analyze_file(temporary / "missing.cue"); },
        "cannot inspect input file");

    const auto oversized = temporary / "oversized.cue";
    {
        std::ofstream stream(oversized, std::ios::binary);
        require(static_cast<bool>(stream), "unable to create oversized sparse file");
        stream.seekp(static_cast<std::streamoff>(cue_charset::max_file_size));
        stream.put('\0');
        require(static_cast<bool>(stream), "unable to size oversized sparse file");
    }
    require_error([&] { (void)analyzer.analyze_file(oversized); }, "128 MiB");

    std::error_code ignored;
    std::filesystem::remove_all(temporary, ignored);
}

void test_track_layout() {
    using cue_charset::detail::TrackDescriptor;
    const std::array tracks{
        TrackDescriptor{1, "file://album.wav", false, 0.0},
        TrackDescriptor{2, "file://album.wav", false, 4.0},
        TrackDescriptor{3, "file://disc-two.wav", false, 1.0}};
    const auto layout = cue_charset::detail::build_track_layout(tracks);
    require(layout.size() == 3, "track layout size changed");
    require(
        layout[0].known_length_seconds == 4.0,
        "same-file track did not end at the next INDEX 01");
    require(
        !layout[1].known_length_seconds.has_value(),
        "track before a FILE change unexpectedly has a known length");
    require(
        !layout[2].known_length_seconds.has_value(),
        "last track unexpectedly has a known length");

    const std::array invalid{
        TrackDescriptor{1, "file://album.wav", false, 4.0},
        TrackDescriptor{2, "file://album.wav", false, 4.0}};
    require_error(
        [&] { (void)cue_charset::detail::build_track_layout(invalid); },
        "INDEX 01 order");

    require_error(
        [] {
            (void)cue_charset::detail::build_track_layout(
                std::span<const TrackDescriptor>{});
        },
        "no audio tracks");

    const std::array duplicate_numbers{
        TrackDescriptor{1, "file://album.wav", false, 0.0},
        TrackDescriptor{1, "file://album.wav", false, 4.0}};
    require_error(
        [&] { (void)cue_charset::detail::build_track_layout(duplicate_numbers); },
        "not strictly increasing");

    const std::array empty_path{TrackDescriptor{1, "", false, 0.0}};
    require_error(
        [&] { (void)cue_charset::detail::build_track_layout(empty_path); },
        "empty referenced path");

    const std::array negative_start{
        TrackDescriptor{1, "file://album.wav", false, -1.0}};
    require_error(
        [&] { (void)cue_charset::detail::build_track_layout(negative_start); },
        "invalid INDEX 01");

    const std::array non_finite_start{
        TrackDescriptor{
            1,
            "file://album.wav",
            false,
            std::numeric_limits<double>::quiet_NaN()}};
    require_error(
        [&] { (void)cue_charset::detail::build_track_layout(non_finite_start); },
        "invalid INDEX 01");

    const std::array binary_track{
        TrackDescriptor{1, "file://disc.bin", true, 0.0}};
    const auto binary_layout = cue_charset::detail::build_track_layout(binary_track);
    require(binary_layout.front().binary, "BINARY file flag was not preserved");

    const std::array non_contiguous{
        TrackDescriptor{1, "file://album.wav", false, 0.0},
        TrackDescriptor{3, "file://album.wav", false, 4.0},
        TrackDescriptor{9, "file://album.wav", false, 8.0}};
    const auto non_contiguous_layout =
        cue_charset::detail::build_track_layout(non_contiguous);
    const auto* track_three =
        cue_charset::detail::find_track(non_contiguous_layout, 3);
    require(track_three != nullptr, "non-contiguous CUE track was not found");
    require(track_three->number == 3, "subsong lookup returned the wrong track");
    require(
        cue_charset::detail::find_track(non_contiguous_layout, 2) == nullptr,
        "invalid subsong unexpectedly resolved to a CUE track");

    const auto location = cue_charset::detail::make_cue_subsong_location(
        "file://D:/Music/Album/album.cue", non_contiguous_layout[1]);
    require(
        location.cue_path == "file://D:/Music/Album/album.cue",
        "playlist entry did not preserve the canonical CUE path");
    require(
        location.subsong == 3,
        "playlist entry subsong did not equal the CUE TRACK number");
    require_error(
        [&] {
            (void)cue_charset::detail::make_cue_subsong_location(
                "", non_contiguous_layout[0]);
        },
        "path is empty");
}

void test_cue_reference_classification() {
    using cue_charset::detail::CueReferenceKind;
    using cue_charset::detail::classify_cue_reference;

    require(
        classify_cue_reference("disc\\track.wav") ==
            CueReferenceKind::relative,
        "Windows relative CUE reference was not accepted");
    require(
        classify_cue_reference("disc/track.wav") ==
            CueReferenceKind::relative,
        "slash-separated relative CUE reference was not accepted");
    require(
        classify_cue_reference("X:\\Fixtures\\Album\\Track - 04.测试.wav") ==
            CueReferenceKind::drive_absolute,
        "drive-absolute CUE reference was not recognized");
    require(
        classify_cue_reference("d:/Music/Album/track.wav") ==
            CueReferenceKind::drive_absolute,
        "slash-separated drive-absolute reference was not recognized");
    require(
        classify_cue_reference("\\\\server\\share\\album\\track.wav") ==
            CueReferenceKind::unc_absolute,
        "backslash UNC CUE reference was not recognized");
    require(
        classify_cue_reference("//server/share/album/track.wav") ==
            CueReferenceKind::unc_absolute,
        "slash UNC CUE reference was not recognized");

    for (const std::string_view invalid : {
             std::string_view{},
             std::string_view{"C:"},
             std::string_view{"C:track.wav"},
             std::string_view{"\\track.wav"},
             std::string_view{"/track.wav"},
             std::string_view{"https://example.test/track.wav"}}) {
        require(
            classify_cue_reference(invalid) == CueReferenceKind::unsupported,
            std::string("ambiguous or non-local CUE reference was accepted: ") +
                std::string(invalid));
    }
}

} // namespace

int main(const int argc, char* argv[]) {
    if (argc != 5) {
        std::cerr
            << "test setup error: expected fixture, ICU, fake ICU, and temporary roots\n";
        return 2;
    }

    try {
        const std::filesystem::path fixtures(argv[1]);
        const std::filesystem::path icu_root(argv[2]);
        const std::filesystem::path fake_icu_root(argv[3]);
        const std::filesystem::path temporary_root(argv[4]);

        const cue_charset::Analyzer analyzer(icu_root);
        test_gb18030_end_to_end(analyzer, fixtures);
        test_corrupt_gb18030(analyzer, fixtures);
        test_unicode_signature(analyzer);
        test_statistical_candidates(analyzer, fixtures);
        cue_charset::tests::run_cue_encoding_tests(analyzer);
        cue_charset::tests::run_flac_duration_repair_tests();
        cue_charset::tests::run_riff_info_tests(analyzer);
        test_track_layout();
        test_cue_reference_classification();
        test_file_errors(analyzer, temporary_root / "cue-charset-tests");

        require_error(
            [&] { const cue_charset::Analyzer invalid(icu_root / "does-not-exist"); },
            "unable to load ICU DLL");
        require_error(
            [&] { const cue_charset::Analyzer missing_symbols(fake_icu_root); },
            "required ICU 78 symbol");

        const std::array<std::byte, 1> value{std::byte{'x'}};
        require_error([&] { (void)analyzer.convert(value, ""); }, "empty charset name");
        require_error(
            [&] { (void)analyzer.convert(value, "not-a-real-charset"); },
            "could not open converter");
        require_error(
            [&] { (void)analyzer.detect(std::span<const std::byte>{}); },
            "empty file");
        const auto empty_conversion =
            analyzer.convert(std::span<const std::byte>{}, "UTF-8");
        require(empty_conversion.utf8.empty(), "empty conversion produced text");
        require(empty_conversion.replacement_count == 0, "empty conversion had replacements");

        std::cout << "all core tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << '\n';
        return 1;
    }
}
