#include "cue_encoding_tests.hpp"
#include "cue_encoding.hpp"

#include <array>
#include <stdexcept>

namespace cue_charset::tests {
namespace {

void require(const bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

template<typename Function>
void expect_error(Function function) {
    try { function(); }
    catch (const Error&) { return; }
    throw std::runtime_error("Expected invalid priority configuration to fail");
}

std::span<const std::byte> bytes(const std::string& text) {
    return std::as_bytes(std::span(text.data(), text.size()));
}

std::optional<detail::CueCandidateIssue> missing_reference() {
    return detail::CueCandidateIssue{detail::CueCandidateStage::referenced_file_check,
        "strict conversion and CUE parsing passed; referenced file does not exist", 1,
        "file://C:\\fixtures\\架空\\Artist - Disc1.wav"};
}

void test_settings(const Analyzer& analyzer) {
    const auto names = detail::parse_encoding_priority(" gb18030 \r\nBig5\nshift_jis\n");
    require(names == std::vector<std::string>{"GB18030", "BIG5", "SHIFT_JIS"},
        "Priority order or name normalization changed");
    require(detail::parse_encoding_priority(detail::serialize_encoding_priority(names)) == names,
        "Priority persistence round-trip changed");
    detail::validate_encoding_priority(analyzer, names);
    require(detail::parse_encoding_priority("").empty(), "Empty priority must mean automatic");
    require(detail::parse_encoding_priority(detail::default_encoding_priority) ==
        std::vector<std::string>{"GB18030"}, "Initial priority must be GB18030");
    expect_error([] { (void)detail::parse_encoding_priority("Big5\nbig5"); });
    expect_error([] { (void)detail::parse_encoding_priority("GB 18030"); });
    expect_error([] { (void)detail::parse_encoding_priority(std::string(65, 'x')); });
    expect_error([&] {
        detail::validate_encoding_priority(analyzer,
            detail::parse_encoding_priority("not-a-real-encoding"));
    });
    std::string excessive;
    for (int index = 0; index < 33; ++index) excessive += "encoding-" + std::to_string(index) + "\n";
    expect_error([&] { (void)detail::parse_encoding_priority(excessive); });
}

void test_selection(const Analyzer& analyzer) {
    using detail::CueEncodingSource;
    // Synthetic CUE: ASCII FILE path, GB18030 bytes for the Japanese title まどろみ.
    // Both GB18030 and Big5 decode strictly, so file existence alone cannot decide.
    const std::string cue = "FILE \"fixture.wav\" WAVE\n  TRACK 01 AUDIO\n    TITLE \""
        "\xA4\xDE\xA4\xC9\xA4\xED\xA4\xDF" "\"\n    INDEX 01 00:00:00\n";
    const auto gb = analyzer.convert(bytes(cue), "GB18030");
    const auto big5 = analyzer.convert(bytes(cue), "Big5");
    require(gb.replacement_count == 0 && big5.replacement_count == 0 && gb.utf8 != big5.utf8,
        "Ambiguous CUE no longer exercises two strict conversions");
    require(gb.utf8.find("まどろみ") != std::string::npos, "Synthetic Japanese title changed");

    const auto accepts = [](const std::string&, std::string_view)
        -> std::optional<detail::CueCandidateIssue> { return std::nullopt; };
    auto priority = detail::parse_encoding_priority("GB18030\nBig5");
    auto result = detail::select_cue_text(analyzer, bytes(cue), priority, accepts);
    require(result.source == CueEncodingSource::priority && result.detection.encoding == "GB18030"
        && result.validated && result.utf8 == gb.utf8, "GB18030 priority was not respected");
    priority = detail::parse_encoding_priority("Big5\nGB18030");
    result = detail::select_cue_text(analyzer, bytes(cue), priority, accepts);
    require(result.detection.encoding == "BIG5" && result.utf8 == big5.utf8,
        "User priority order was ignored");

    int rejected = 0;
    result = detail::select_cue_text(analyzer, bytes(cue), priority,
        [&](const std::string& text, std::string_view) -> std::optional<detail::CueCandidateIssue> {
            if (text == big5.utf8) { ++rejected; return missing_reference(); }
            return text == gb.utf8 ? std::nullopt : missing_reference();
        });
    require(rejected == 1 && result.detection.encoding == "GB18030",
        "Invalid syntax or missing referenced-file candidate was accepted");
    require(result.rejections.size() == 1 &&
        result.rejections.front().issue.stage == detail::CueCandidateStage::referenced_file_check,
        "Missing reference rejection was lost");
    require(detail::describe_cue_selection(result).find("referenced=") == std::string::npos,
        "A validated selection must not retain unrelated missing-reference diagnostics");

    priority = detail::parse_encoding_priority("UTF-8\nGB18030");
    result = detail::select_cue_text(analyzer, bytes(cue), priority, accepts);
    require(result.detection.encoding == "GB18030", "Replacement-containing priority was accepted");
    require(result.rejections.size() == 1 &&
        result.rejections.front().issue.stage == detail::CueCandidateStage::conversion &&
        result.rejections.front().issue.reason.find("first_source_byte_offset=") != std::string::npos,
        "Strict conversion rejection must include replacement count and byte offset");

    const auto automatic = analyzer.detect(bytes(cue));
    result = detail::select_cue_text(analyzer, bytes(cue), {}, accepts);
    require(result.source == CueEncodingSource::automatic &&
        result.detection.encoding == automatic.encoding, "Empty list changed automatic detection");
    priority = detail::parse_encoding_priority("UTF-8");
    result = detail::select_cue_text(analyzer, bytes(cue), priority, accepts);
    require(result.source == CueEncodingSource::automatic,
        "Rejected priority did not fall back to automatic detection");

    priority = detail::parse_encoding_priority("GB18030");
    result = detail::select_cue_text(analyzer, bytes(cue), priority,
        [](const std::string&, std::string_view) { return missing_reference(); });
    require(!result.validated && result.detection.encoding == automatic.encoding,
        "All-rejected behavior no longer exposes the original ICU candidate");
    const auto diagnostic = detail::describe_cue_selection(result);
    require(!result.rejections.empty() && result.rejections.front().encoding == "GB18030" &&
        result.rejections.front().source == CueEncodingSource::priority,
        "Priority rejection must survive automatic fallback");
    require(diagnostic.find("first ICU candidate retained (not validated)") != std::string::npos &&
        diagnostic.find("candidate=\"GB18030\", source=priority") != std::string::npos &&
        diagnostic.find("track=1") != std::string::npos &&
        diagnostic.find("Artist - Disc1.wav") != std::string::npos &&
        diagnostic.find("CUE parsing passed") != std::string::npos,
        "Terminal diagnostics lost the correct candidate's missing reference");
    const auto fallback_conversion = analyzer.convert(bytes(cue), automatic.encoding);
    require(result.replacement_count == fallback_conversion.replacement_count &&
        result.first_replacement_offset == fallback_conversion.first_replacement_offset,
        "Unchecked fallback conversion diagnostics are inaccurate");

    struct Cancelled {};
    bool cancelled = false;
    try {
        (void)detail::select_cue_text(analyzer, bytes(cue), priority,
            [](const std::string&, std::string_view) -> std::optional<detail::CueCandidateIssue> {
                throw Cancelled{};
            });
    } catch (const Cancelled&) { cancelled = true; }
    require(cancelled, "Validator cancellation was swallowed");

    int validations = 0;
    const std::string bom = "\xEF\xBB\xBF" "TITLE \"まどろみ\"\n";
    priority = detail::parse_encoding_priority("Big5\nGB18030");
    result = detail::select_cue_text(analyzer, bytes(bom), priority,
        [&](const std::string&, std::string_view) { ++validations; return missing_reference(); });
    require(result.source == CueEncodingSource::signature && validations == 0 &&
        result.utf8 == "TITLE \"まどろみ\"\n", "BOM did not override the priority list");
    const std::string utf16("\xFF\xFE" "A\0", 4);
    result = detail::select_cue_text(analyzer, bytes(utf16), priority, accepts);
    require(result.source == CueEncodingSource::signature && result.utf8 == "A",
        "UTF-16 BOM was not authoritative");
    require(result.rejections.empty() &&
        detail::describe_cue_selection(result).find("no candidate passed") == std::string::npos,
        "Signature path must not claim that statistical candidates were rejected");
    const std::string damaged_bom = "\xEF\xBB\xBF\xFF";
    result = detail::select_cue_text(analyzer, bytes(damaged_bom), priority, accepts);
    require(result.source == CueEncodingSource::signature && result.replacement_count == 1 &&
        result.first_replacement_offset == 3,
        "Signature conversion diagnostics must report source offsets including BOM bytes");
    expect_error([&] { (void)detail::select_cue_text(analyzer, {}, priority, accepts); });

    priority = detail::parse_encoding_priority("not-a-real-encoding\nGB18030");
    result = detail::select_cue_text(analyzer, bytes(cue), priority, accepts);
    require(result.rejections.size() == 1 &&
        result.rejections.front().issue.stage == detail::CueCandidateStage::conversion &&
        !result.rejections.front().issue.reason.empty(), "Converter failure reason was lost");

    result = detail::select_cue_text(analyzer, bytes(cue), {},
        [](const std::string&, std::string_view) -> std::optional<detail::CueCandidateIssue> {
            return detail::CueCandidateIssue{detail::CueCandidateStage::cue_parse_path_resolution,
                "invalid INDEX", {}, {}};
        });
    require(detail::describe_cue_selection(result).find(
        "stage=cue-parse-path-resolution, reason=\"invalid INDEX\"") != std::string::npos,
        "Syntax rejection must remain distinct from missing audio");

    bool io_propagated = false;
    try {
        (void)detail::select_cue_text(analyzer, bytes(cue), {},
            [](const std::string&, std::string_view) -> std::optional<detail::CueCandidateIssue> {
                throw std::runtime_error("access denied");
            });
    } catch (const std::runtime_error& error) {
        io_propagated = std::string_view(error.what()) == "access denied";
    }
    require(io_propagated, "I/O errors must propagate, not become candidate rejection");
    require(detail::quote_diagnostic_value("a\"\n\r\t\\b") == "\"a\\\"\\x0A\\x0D\\x09\\\\b\"",
        "Diagnostic quoting permits injected lines or corrupts path escaping");
    require(detail::quote_diagnostic_value("日本語") == "\"日本語\"",
        "Diagnostic quoting must preserve UTF-8");
    detail::SelectedCueText offset_zero;
    offset_zero.source = CueEncodingSource::automatic;
    offset_zero.detection.encoding = "UTF-8";
    offset_zero.replacement_count = 1;
    offset_zero.first_replacement_offset = 0;
    require(detail::describe_cue_selection(offset_zero).find("first_source_byte_offset=0") !=
        std::string::npos, "Byte offset zero must not be mistaken for an absent offset");
}

} // namespace

void run_cue_encoding_tests(const Analyzer& analyzer) {
    test_settings(analyzer);
    test_selection(analyzer);
}

} // namespace cue_charset::tests
