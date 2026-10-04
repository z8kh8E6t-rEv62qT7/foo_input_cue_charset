#include "cue_encoding.hpp"

#include <algorithm>
#include <utility>

namespace cue_charset::detail {
namespace {

std::string normalize_name(std::string_view name) {
    const auto first = name.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) return {};
    name = name.substr(first, name.find_last_not_of(" \t\r") - first + 1);
    if (name.size() > max_encoding_name_length) {
        throw Error("Encoding name exceeds 64 characters");
    }
    std::string result;
    for (const char ch : name) {
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.')) {
            throw Error("Use an ICU encoding name, e.g. GB18030, Big5 or Shift_JIS");
        }
        result += ch >= 'a' && ch <= 'z' ? static_cast<char>(ch - 'a' + 'A') : ch;
    }
    return result;
}

ConversionResult convert_text(const Analyzer& analyzer,
    const std::span<const std::byte> bytes, const std::string_view encoding) {
    auto conversion = analyzer.convert(bytes, encoding);
    if (conversion.utf8.starts_with("\xEF\xBB\xBF")) conversion.utf8.erase(0, 3);
    return conversion;
}

std::optional<std::string> try_candidate(const Analyzer& analyzer,
    const std::span<const std::byte> bytes, const std::string_view encoding,
    const CueCandidateValidator& validator, const CueEncodingSource source,
    std::vector<CueCandidateRejection>& rejections) {
    const auto reject = [&](CueCandidateIssue issue) {
        rejections.push_back({std::string(encoding), source, std::move(issue)});
    };
    ConversionResult conversion;
    try {
        conversion = analyzer.convert(bytes, encoding);
    } catch (const Error& error) {
        reject({CueCandidateStage::conversion, error.what(), {}, {}});
        return std::nullopt;
    }
    if (conversion.replacement_count != 0) {
        std::string reason = "strict conversion rejected: replacements=" +
            std::to_string(conversion.replacement_count);
        if (conversion.first_replacement_offset) {
            reason += ", first_source_byte_offset=" +
                std::to_string(*conversion.first_replacement_offset);
        }
        reject({CueCandidateStage::conversion, std::move(reason), {}, {}});
        return std::nullopt;
    }
    if (conversion.utf8.starts_with("\xEF\xBB\xBF")) conversion.utf8.erase(0, 3);
    if (auto issue = validator(conversion.utf8, encoding)) {
        reject(std::move(*issue));
        return std::nullopt;
    }
    return std::move(conversion.utf8);
}

} // namespace

std::string quote_diagnostic_value(const std::string_view text) {
    std::string result = "\"";
    constexpr char hex[] = "0123456789ABCDEF";
    for (const unsigned char ch : text) {
        if (ch == '"' || ch == '\\') {
            result += '\\';
            result += static_cast<char>(ch);
        } else if (ch < 0x20 || ch == 0x7F) {
            result += "\\x";
            result += hex[ch >> 4];
            result += hex[ch & 0x0F];
        } else {
            result += static_cast<char>(ch);
        }
    }
    return result + '"';
}

std::string describe_cue_selection(const SelectedCueText& selected) {
    const auto source_name = [](const CueEncodingSource source) {
        switch (source) {
        case CueEncodingSource::signature: return "signature";
        case CueEncodingSource::priority: return "priority";
        case CueEncodingSource::automatic: return "automatic";
        case CueEncodingSource::automatic_fallback: return "automatic-fallback";
        }
        return "unknown";
    };
    const auto stage_name = [](const CueCandidateStage stage) {
        switch (stage) {
        case CueCandidateStage::conversion: return "conversion";
        case CueCandidateStage::cue_parse_path_resolution: return "cue-parse-path-resolution";
        case CueCandidateStage::referenced_file_check: return "referenced-file-check";
        }
        return "unknown";
    };
    std::string result = "encoding=" + quote_diagnostic_value(selected.detection.encoding) +
        ", source=" + source_name(selected.source) +
        ", validated=" + (selected.validated ? "true" : "false") +
        ", replacements=" + std::to_string(selected.replacement_count);
    if (selected.detection.confidence) {
        result += ", confidence=" + std::to_string(*selected.detection.confidence);
    }
    if (selected.first_replacement_offset) {
        result += ", first_source_byte_offset=" + std::to_string(*selected.first_replacement_offset);
    }
    if (!selected.validated && !selected.rejections.empty()) {
        result += "; no candidate passed validation; first ICU candidate retained (not validated)";
        for (const auto& rejected : selected.rejections) {
            result += "\n  candidate=" + quote_diagnostic_value(rejected.encoding) +
                ", source=" + source_name(rejected.source) +
                ", stage=" + stage_name(rejected.issue.stage);
            if (rejected.issue.track) result += ", track=" + std::to_string(*rejected.issue.track);
            if (!rejected.issue.referenced_path.empty()) {
                result += ", referenced=" + quote_diagnostic_value(rejected.issue.referenced_path);
            }
            result += ", reason=" + quote_diagnostic_value(rejected.issue.reason);
        }
    }
    return result;
}

std::vector<std::string> parse_encoding_priority(const std::string_view text) {
    std::vector<std::string> result;
    std::size_t offset = 0;
    while (offset < text.size()) {
        const auto end = text.find('\n', offset);
        auto name = normalize_name(text.substr(offset,
            end == std::string_view::npos ? text.size() - offset : end - offset));
        if (!name.empty()) {
            if (std::find(result.begin(), result.end(), name) != result.end()) {
                throw Error("Duplicate encoding: " + name);
            }
            if (result.size() == max_encoding_priorities) {
                throw Error("At most 32 priority encodings are supported");
            }
            result.push_back(std::move(name));
        }
        if (end == std::string_view::npos) break;
        offset = end + 1;
    }
    return result;
}

std::string serialize_encoding_priority(const std::span<const std::string> encodings) {
    std::string text;
    for (const auto& name : encodings) {
        if (!text.empty()) text += '\n';
        text += name;
    }
    return text;
}

void validate_encoding_priority(const Analyzer& analyzer,
    const std::span<const std::string> encodings) {
    for (const auto& name : encodings) {
        (void)analyzer.convert({}, name);
    }
}

SelectedCueText select_cue_text(const Analyzer& analyzer,
    const std::span<const std::byte> bytes,
    const std::span<const std::string> priorities,
    const CueCandidateValidator& validator) {
    if (const auto signature = analyzer.detect_unicode_signature(bytes)) {
        auto conversion = convert_text(analyzer, bytes, signature->encoding);
        return {std::move(conversion.utf8), *signature,
            CueEncodingSource::signature, {}, false, conversion.replacement_count,
            conversion.first_replacement_offset, {}};
    }
    std::vector<CueCandidateRejection> rejections;
    for (const auto& encoding : priorities) {
        if (auto text = try_candidate(analyzer, bytes, encoding, validator,
                CueEncodingSource::priority, rejections)) {
            return {std::move(*text), {std::nullopt, 0, encoding, std::nullopt},
                CueEncodingSource::priority, {}, true, 0, {}, std::move(rejections)};
        }
    }

    const auto candidates = analyzer.detect_candidates(bytes);
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (auto text = try_candidate(analyzer, bytes, candidates[index].encoding, validator,
                CueEncodingSource::automatic, rejections)) {
            return {std::move(*text), candidates[index],
                index == 0 ? CueEncodingSource::automatic : CueEncodingSource::automatic_fallback,
                candidates.front().encoding, true, 0, {}, std::move(rejections)};
        }
    }
    // Preserve the existing last-resort behavior: expose the first ICU result
    // to the parser, so malformed CUEs still produce a meaningful host error.
    auto conversion = convert_text(analyzer, bytes, candidates.front().encoding);
    return {std::move(conversion.utf8), candidates.front(), CueEncodingSource::automatic,
        candidates.front().encoding, false, conversion.replacement_count,
        conversion.first_replacement_offset, std::move(rejections)};
}

} // namespace cue_charset::detail
