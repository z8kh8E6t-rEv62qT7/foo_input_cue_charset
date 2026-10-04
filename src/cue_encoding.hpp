#pragma once

#include "cue_charset/cue_charset.hpp"

#include <functional>
#include <cstdint>

namespace cue_charset::detail {

inline constexpr std::string_view default_encoding_priority = "GB18030";
inline constexpr std::size_t max_encoding_name_length = 64;
inline constexpr std::size_t max_encoding_priorities = 32;

// Persisted as one ASCII ICU converter name per line; empty means automatic.
[[nodiscard]] std::vector<std::string> parse_encoding_priority(std::string_view text);
[[nodiscard]] std::string serialize_encoding_priority(
    std::span<const std::string> encodings);
void validate_encoding_priority(const Analyzer& analyzer,
    std::span<const std::string> encodings);

enum class CueEncodingSource { signature, priority, automatic, automatic_fallback };

enum class CueCandidateStage { conversion, cue_parse_path_resolution, referenced_file_check };

struct CueCandidateIssue {
    CueCandidateStage stage;
    std::string reason;
    std::optional<std::uint32_t> track;
    std::string referenced_path;
};

struct CueCandidateRejection {
    std::string encoding;
    CueEncodingSource source;
    CueCandidateIssue issue;
};

using CueCandidateValidator =
    std::function<std::optional<CueCandidateIssue>(const std::string&, std::string_view)>;

struct SelectedCueText {
    std::string utf8;
    DetectionResult detection;
    CueEncodingSource source;
    std::string first_automatic_encoding;
    bool validated = false;
    std::size_t replacement_count = 0;
    std::optional<std::size_t> first_replacement_offset;
    std::vector<CueCandidateRejection> rejections;
};

// Quote untrusted path/error text without allowing injected Console lines.
[[nodiscard]] std::string quote_diagnostic_value(std::string_view text);
// Successful validation needs only the selected encoding. Unvalidated fallback
// includes the rejected candidates, without claiming any encoding is correct.
[[nodiscard]] std::string describe_cue_selection(const SelectedCueText& selected);

// Validator checks CUE syntax and referenced-file existence in the host.
// nullopt means valid; a structured issue means rejected. Exceptions propagate.
[[nodiscard]] SelectedCueText select_cue_text(
    const Analyzer& analyzer,
    std::span<const std::byte> bytes,
    std::span<const std::string> priorities,
    const CueCandidateValidator& validator);

} // namespace cue_charset::detail
