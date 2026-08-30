#pragma once

#include <string_view>

namespace cue_charset::detail {

enum class CueReferenceKind {
    relative,
    drive_absolute,
    unc_absolute,
    unsupported,
};

[[nodiscard]] CueReferenceKind classify_cue_reference(
    std::string_view reference) noexcept;

} // namespace cue_charset::detail
