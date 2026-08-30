#include "cue_reference_path.hpp"

namespace cue_charset::detail {
namespace {

constexpr bool is_separator(const char value) noexcept {
    return value == '\\' || value == '/';
}

constexpr bool is_ascii_letter(const char value) noexcept {
    return (value >= 'A' && value <= 'Z') ||
           (value >= 'a' && value <= 'z');
}

} // namespace

CueReferenceKind classify_cue_reference(
    const std::string_view reference) noexcept {
    if (reference.empty() ||
        reference.find("://") != std::string_view::npos) {
        return CueReferenceKind::unsupported;
    }

    if (reference.size() >= 3 &&
        is_ascii_letter(reference[0]) &&
        reference[1] == ':' &&
        is_separator(reference[2])) {
        return CueReferenceKind::drive_absolute;
    }

    if (reference.size() >= 2 &&
        is_separator(reference[0]) &&
        is_separator(reference[1])) {
        return CueReferenceKind::unc_absolute;
    }

    if (is_separator(reference[0]) ||
        (reference.size() >= 2 && reference[1] == ':')) {
        return CueReferenceKind::unsupported;
    }

    return CueReferenceKind::relative;
}

} // namespace cue_charset::detail
