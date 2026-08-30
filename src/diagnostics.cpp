#include "cue_charset/cue_charset.hpp"

#include <sstream>

namespace cue_charset {
namespace {

std::string path_as_utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

} // namespace

std::string format_diagnostics(const AnalysisResult& result) {
    std::ostringstream output;
    output << "path=" << path_as_utf8(result.path) << '\n';
    output << "bytes=" << result.byte_count << '\n';
    output << "unicode_signature=";
    if (result.detection.unicode_signature.has_value()) {
        output << *result.detection.unicode_signature << " ("
               << result.detection.signature_length << " bytes)\n";
    } else {
        output << "none\n";
    }
    output << "encoding=" << result.detection.encoding << '\n';
    output << "confidence=";
    if (result.detection.confidence.has_value()) {
        output << *result.detection.confidence << '\n';
    } else {
        output << "N/A (signature)\n";
    }
    output << "conversion=success\n";
    output << "replacements=" << result.conversion.replacement_count << '\n';
    output << "first_replacement_offset=";
    if (result.conversion.first_replacement_offset.has_value()) {
        output << *result.conversion.first_replacement_offset << '\n';
    } else {
        output << "N/A\n";
    }
    return output.str();
}

} // namespace cue_charset

