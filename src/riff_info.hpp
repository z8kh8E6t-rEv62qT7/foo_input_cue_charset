#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace cue_charset {

class Analyzer;

namespace detail {

inline constexpr std::uint64_t max_riff_info_field_size = 1U * 1024U * 1024U;
inline constexpr std::uint64_t max_riff_info_total_size = 4U * 1024U * 1024U;
inline constexpr std::uint64_t riff_info_recovery_window_size =
    4U * 1024U * 1024U;

enum class RiffInfoField {
    title,
    artist,
    album,
    track_number,
    date,
    genre,
    comment,
    encoder,
};

struct RiffInfoTag {
    RiffInfoField field{};
    std::string value;
};

enum class RiffInfoUtf8Status {
    applied,
    not_riff_wave,
    no_supported_fields,
    ascii_only,
    invalid_text,
};

enum class RiffInfoReadMode {
    none,
    structured,
    structured_truncated_data,
    recovery_head,
    recovery_tail,
};

struct RiffInfoUtf8Result {
    RiffInfoUtf8Status status = RiffInfoUtf8Status::not_riff_wave;
    std::vector<RiffInfoTag> tags;
    std::string reason;
    RiffInfoReadMode mode = RiffInfoReadMode::none;
};

[[nodiscard]] const char* riff_info_read_mode_name(
    RiffInfoReadMode mode) noexcept;

using RiffReadAt =
    std::function<void(std::uint64_t offset, std::span<std::byte> output)>;

[[nodiscard]] RiffInfoUtf8Result read_riff_info_utf8(
    std::uint64_t file_size,
    const RiffReadAt& read_at,
    const Analyzer& analyzer);

} // namespace detail
} // namespace cue_charset
