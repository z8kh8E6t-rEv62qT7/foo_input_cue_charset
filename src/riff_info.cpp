#include "riff_info.hpp"

#include "cue_charset/cue_charset.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <iterator>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

namespace cue_charset::detail {
namespace {

constexpr std::uint32_t fourcc(
    const char first,
    const char second,
    const char third,
    const char fourth) noexcept {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(first)) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(second)) << 8U) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(third)) << 16U) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(fourth)) << 24U);
}

constexpr std::uint32_t riff_id = fourcc('R', 'I', 'F', 'F');
constexpr std::uint32_t wave_id = fourcc('W', 'A', 'V', 'E');
constexpr std::uint32_t list_id = fourcc('L', 'I', 'S', 'T');
constexpr std::uint32_t info_id = fourcc('I', 'N', 'F', 'O');
constexpr std::uint32_t data_id = fourcc('d', 'a', 't', 'a');

constexpr std::uint32_t title_id = fourcc('I', 'N', 'A', 'M');
constexpr std::uint32_t artist_id = fourcc('I', 'A', 'R', 'T');
constexpr std::uint32_t album_id = fourcc('I', 'P', 'R', 'D');
constexpr std::uint32_t track_id = fourcc('I', 'T', 'R', 'K');
constexpr std::uint32_t legacy_track_id = fourcc('I', 'P', 'R', 'T');
constexpr std::uint32_t date_id = fourcc('I', 'C', 'R', 'D');
constexpr std::uint32_t genre_id = fourcc('I', 'G', 'N', 'R');
constexpr std::uint32_t comment_id = fourcc('I', 'C', 'M', 'T');
constexpr std::uint32_t encoder_id = fourcc('I', 'S', 'F', 'T');

std::uint32_t read_u32_le(const std::span<const std::byte> bytes) {
    if (bytes.size() < sizeof(std::uint32_t)) {
        throw Error("truncated RIFF 32-bit value");
    }
    return static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[0])) |
           (static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[1])) << 8U) |
           (static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[2])) << 16U) |
           (static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[3])) << 24U);
}

std::uint64_t checked_add(
    const std::uint64_t left,
    const std::uint64_t right,
    const std::string_view context) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        throw Error("RIFF size overflow while " + std::string(context));
    }
    return left + right;
}

std::uint64_t padded_size(const std::uint64_t size) {
    return checked_add(size, size & 1U, "applying chunk padding");
}

bool is_supported_field(const std::uint32_t id) noexcept {
    switch (id) {
    case title_id:
    case artist_id:
    case album_id:
    case track_id:
    case legacy_track_id:
    case date_id:
    case genre_id:
    case comment_id:
    case encoder_id:
        return true;
    default:
        return false;
    }
}

struct RawInfoEntry {
    std::uint32_t id = 0;
    std::vector<std::byte> value;
};

struct RawInfoFields {
    std::vector<RawInfoEntry> entries;
    std::uint64_t total_size = 0;
};

void read_supported_field(
    RawInfoFields& fields,
    const std::uint32_t id,
    const std::uint64_t offset,
    const std::uint32_t size,
    const RiffReadAt& read_at) {
    if (!is_supported_field(id)) {
        return;
    }
    if (size > max_riff_info_field_size) {
        throw Error("RIFF INFO field exceeds the 1 MiB limit");
    }
    if (size > max_riff_info_total_size - fields.total_size) {
        throw Error("supported RIFF INFO fields exceed the 4 MiB total limit");
    }

    std::vector<std::byte> value(size);
    if (!value.empty()) {
        read_at(offset, value);
    }
    fields.entries.push_back(RawInfoEntry{id, std::move(value)});
    fields.total_size += size;
}

void parse_info_list(
    RawInfoFields& fields,
    std::uint64_t offset,
    const std::uint64_t end,
    const RiffReadAt& read_at) {
    std::array<std::byte, 8> header{};
    while (offset < end) {
        if (end - offset < header.size()) {
            throw Error("truncated RIFF INFO subchunk header");
        }
        read_at(offset, header);
        const std::uint32_t id = read_u32_le(std::span<const std::byte>(header).first<4>());
        const std::uint32_t size = read_u32_le(std::span<const std::byte>(header).subspan<4>());
        const std::uint64_t payload = checked_add(offset, header.size(), "locating INFO payload");
        const std::uint64_t payload_end = checked_add(payload, size, "locating INFO payload end");
        const std::uint64_t next = checked_add(payload, padded_size(size), "locating next INFO field");
        if (payload_end > end || next > end) {
            throw Error("RIFF INFO subchunk exceeds its LIST boundary");
        }

        read_supported_field(fields, id, payload, size, read_at);
        offset = next;
    }
}

struct StructuredResult {
    RawInfoFields fields;
    RiffInfoReadMode mode = RiffInfoReadMode::structured;
};

StructuredResult parse_structured(
    const std::uint64_t file_size,
    const RiffReadAt& read_at) {
    std::array<std::byte, 12> riff_header{};
    read_at(0, riff_header);
    const auto header = std::span<const std::byte>(riff_header);

    const std::uint64_t declared_end = checked_add(
        8,
        read_u32_le(header.subspan<4, 4>()),
        "calculating the RIFF boundary");
    if (declared_end < riff_header.size()) {
        throw Error("RIFF declared size is smaller than the WAVE header");
    }
    const std::uint64_t riff_end = std::min(declared_end, file_size);

    StructuredResult result;
    std::array<std::byte, 8> chunk_header{};
    std::array<std::byte, 4> list_type{};
    std::uint64_t offset = riff_header.size();
    while (offset < riff_end) {
        if (riff_end - offset < chunk_header.size()) {
            throw Error("truncated top-level RIFF chunk header");
        }
        read_at(offset, chunk_header);
        const std::uint32_t id =
            read_u32_le(std::span<const std::byte>(chunk_header).first<4>());
        const std::uint32_t size =
            read_u32_le(std::span<const std::byte>(chunk_header).subspan<4>());
        const std::uint64_t payload =
            checked_add(offset, chunk_header.size(), "locating RIFF chunk payload");
        const std::uint64_t payload_end =
            checked_add(payload, size, "locating RIFF chunk end");
        const std::uint64_t next =
            checked_add(payload, padded_size(size), "locating next RIFF chunk");
        if (payload_end > riff_end || next > riff_end) {
            if (id == data_id && payload <= riff_end) {
                result.mode = RiffInfoReadMode::structured_truncated_data;
                break;
            }
            throw Error("top-level RIFF chunk exceeds the RIFF boundary");
        }

        if (id == list_id) {
            if (size < list_type.size()) {
                throw Error("RIFF LIST chunk is too small to contain a list type");
            }
            read_at(payload, list_type);
            if (read_u32_le(list_type) == info_id) {
                parse_info_list(
                    result.fields,
                    checked_add(payload, list_type.size(), "locating INFO list contents"),
                    payload_end,
                    read_at);
            }
        }
        offset = next;
    }
    return result;
}

struct RecoveryCandidate {
    std::uint64_t offset = 0;
    RiffInfoReadMode mode = RiffInfoReadMode::none;
};

void find_candidates_in_window(
    std::vector<RecoveryCandidate>& candidates,
    const std::uint64_t begin,
    const std::uint64_t end,
    const RiffInfoReadMode mode,
    const RiffReadAt& read_at) {
    if (end <= begin || end - begin < 12) {
        return;
    }

    std::vector<std::byte> window(static_cast<std::size_t>(end - begin));
    read_at(begin, window);
    std::uint64_t offset = (begin & 1U) == 0 ? begin : begin + 1;
    for (; offset + 12 <= end; offset += 2) {
        const std::size_t local = static_cast<std::size_t>(offset - begin);
        const auto bytes = std::span<const std::byte>(window).subspan(local, 12);
        if (read_u32_le(bytes.first<4>()) != list_id ||
            read_u32_le(bytes.subspan<8, 4>()) != info_id) {
            continue;
        }
        if (std::none_of(
                candidates.begin(), candidates.end(),
                [offset](const RecoveryCandidate& candidate) {
                    return candidate.offset == offset;
                })) {
            candidates.push_back(RecoveryCandidate{offset, mode});
        }
    }
}

void merge_fields(RawInfoFields& destination, RawInfoFields source) {
    if (source.total_size > max_riff_info_total_size - destination.total_size) {
        throw Error("supported RIFF INFO fields exceed the 4 MiB total limit");
    }
    destination.total_size += source.total_size;
    destination.entries.insert(
        destination.entries.end(),
        std::make_move_iterator(source.entries.begin()),
        std::make_move_iterator(source.entries.end()));
}

std::optional<StructuredResult> recover_fields(
    const std::uint64_t file_size,
    const RiffReadAt& read_at) {
    std::vector<RecoveryCandidate> candidates;
    const std::uint64_t head_end =
        std::min(file_size, riff_info_recovery_window_size);
    find_candidates_in_window(
        candidates, 0, head_end, RiffInfoReadMode::recovery_head, read_at);

    const std::uint64_t tail_begin =
        file_size > riff_info_recovery_window_size
            ? file_size - riff_info_recovery_window_size
            : 0;
    find_candidates_in_window(
        candidates,
        tail_begin,
        file_size,
        RiffInfoReadMode::recovery_tail,
        read_at);
    std::sort(
        candidates.begin(), candidates.end(),
        [](const RecoveryCandidate& left, const RecoveryCandidate& right) {
            return left.offset < right.offset;
        });

    StructuredResult recovered;
    recovered.mode = RiffInfoReadMode::none;
    for (const auto& candidate : candidates) {
        try {
            std::array<std::byte, 12> header{};
            read_at(candidate.offset, header);
            const auto bytes = std::span<const std::byte>(header);
            const std::uint32_t size = read_u32_le(bytes.subspan<4, 4>());
            if (size < 4) {
                continue;
            }
            const std::uint64_t payload =
                checked_add(candidate.offset, 8, "locating recovered LIST payload");
            const std::uint64_t payload_end =
                checked_add(payload, size, "locating recovered LIST end");
            const std::uint64_t next = checked_add(
                payload, padded_size(size), "locating recovered LIST padding");
            if (payload_end > file_size || next > file_size) {
                continue;
            }

            RawInfoFields fields;
            parse_info_list(
                fields,
                checked_add(payload, 4, "locating recovered INFO contents"),
                payload_end,
                read_at);
            if (fields.entries.empty()) {
                continue;
            }
            if (recovered.mode == RiffInfoReadMode::none) {
                recovered.mode = candidate.mode;
            }
            merge_fields(recovered.fields, std::move(fields));
        } catch (const Error&) {
            // A raw LIST/INFO signature is only a candidate. Reject malformed
            // candidates and continue looking within the bounded windows.
        }
    }
    if (recovered.fields.entries.empty()) {
        return std::nullopt;
    }
    return recovered;
}

struct DecodedField {
    bool valid = true;
    bool non_ascii = false;
    std::string value;
    std::string reason;
};

DecodedField decode_field(
    const std::span<const std::byte> raw,
    const Analyzer& analyzer) {
    DecodedField result;
    auto text = raw;
    const auto terminator = std::find(text.begin(), text.end(), std::byte{0});
    if (terminator != text.end()) {
        const auto terminator_index =
            static_cast<std::size_t>(terminator - text.begin());
        const auto trailing = text.subspan(terminator_index + 1);
        if (std::any_of(
                trailing.begin(), trailing.end(),
                [](const std::byte value) { return value != std::byte{0}; })) {
            result.valid = false;
            result.reason = "non-zero bytes follow a RIFF INFO NUL terminator";
            return result;
        }
        text = text.first(terminator_index);
    }

    result.non_ascii = std::any_of(
        text.begin(), text.end(),
        [](const std::byte value) {
            return std::to_integer<unsigned char>(value) >= 0x80U;
        });
    if (text.empty()) {
        return result;
    }

    const auto converted = analyzer.convert(text, "UTF-8");
    if (converted.replacement_count != 0) {
        result.valid = false;
        result.reason = "a supported RIFF INFO field is not strict UTF-8";
        return result;
    }
    result.value = converted.utf8;
    return result;
}

struct DecodedEntry {
    std::uint32_t id = 0;
    DecodedField field;
};

const DecodedField* find_first(
    const std::vector<DecodedEntry>& entries,
    const std::uint32_t id) noexcept {
    const auto found = std::find_if(
        entries.begin(), entries.end(),
        [id](const DecodedEntry& entry) { return entry.id == id; });
    return found == entries.end() ? nullptr : &found->field;
}

void append_tag(
    std::vector<RiffInfoTag>& tags,
    const RiffInfoField field,
    const DecodedField* decoded) {
    if (decoded != nullptr && !decoded->value.empty()) {
        tags.push_back(RiffInfoTag{field, decoded->value});
    }
}

RiffInfoUtf8Result decode_fields(
    const RawInfoFields& raw,
    const Analyzer& analyzer,
    const RiffInfoReadMode mode) {
    if (raw.entries.empty()) {
        return RiffInfoUtf8Result{
            RiffInfoUtf8Status::no_supported_fields, {}, {}, mode};
    }

    std::vector<DecodedEntry> decoded;
    decoded.reserve(raw.entries.size());
    for (const auto& entry : raw.entries) {
        decoded.push_back(DecodedEntry{
            entry.id,
            decode_field(entry.value, analyzer)});
    }

    const auto invalid = std::find_if(
        decoded.begin(), decoded.end(),
        [](const DecodedEntry& entry) { return !entry.field.valid; });
    if (invalid != decoded.end()) {
        return RiffInfoUtf8Result{
            RiffInfoUtf8Status::invalid_text,
            {},
            invalid->field.reason,
            mode};
    }

    const bool any_non_ascii = std::any_of(
        decoded.begin(), decoded.end(),
        [](const DecodedEntry& entry) { return entry.field.non_ascii; });
    if (!any_non_ascii) {
        return RiffInfoUtf8Result{
            RiffInfoUtf8Status::ascii_only, {}, {}, mode};
    }

    std::vector<RiffInfoTag> tags;
    tags.reserve(8);
    append_tag(tags, RiffInfoField::title, find_first(decoded, title_id));
    append_tag(tags, RiffInfoField::artist, find_first(decoded, artist_id));
    append_tag(tags, RiffInfoField::album, find_first(decoded, album_id));
    const DecodedField* track = find_first(decoded, track_id);
    if (track == nullptr || track->value.empty()) {
        track = find_first(decoded, legacy_track_id);
    }
    append_tag(tags, RiffInfoField::track_number, track);
    append_tag(tags, RiffInfoField::date, find_first(decoded, date_id));
    append_tag(tags, RiffInfoField::genre, find_first(decoded, genre_id));
    append_tag(tags, RiffInfoField::comment, find_first(decoded, comment_id));
    append_tag(tags, RiffInfoField::encoder, find_first(decoded, encoder_id));

    return RiffInfoUtf8Result{
        RiffInfoUtf8Status::applied, std::move(tags), {}, mode};
}

} // namespace

const char* riff_info_read_mode_name(const RiffInfoReadMode mode) noexcept {
    switch (mode) {
    case RiffInfoReadMode::structured:
        return "structured";
    case RiffInfoReadMode::structured_truncated_data:
        return "structured-truncated-data";
    case RiffInfoReadMode::recovery_head:
        return "recovery-head";
    case RiffInfoReadMode::recovery_tail:
        return "recovery-tail";
    case RiffInfoReadMode::none:
    default:
        return "none";
    }
}

RiffInfoUtf8Result read_riff_info_utf8(
    const std::uint64_t file_size,
    const RiffReadAt& read_at,
    const Analyzer& analyzer) {
    if (!read_at) {
        throw Error("RIFF reader callback is empty");
    }
    if (file_size < 12) {
        return RiffInfoUtf8Result{
            RiffInfoUtf8Status::not_riff_wave, {}, {}, RiffInfoReadMode::none};
    }

    std::array<std::byte, 12> riff_header{};
    read_at(0, riff_header);
    const auto header = std::span<const std::byte>(riff_header);
    if (read_u32_le(header.first<4>()) != riff_id ||
        read_u32_le(header.subspan<8, 4>()) != wave_id) {
        return RiffInfoUtf8Result{
            RiffInfoUtf8Status::not_riff_wave, {}, {}, RiffInfoReadMode::none};
    }

    std::exception_ptr structured_error;
    try {
        const StructuredResult structured = parse_structured(file_size, read_at);
        if (!structured.fields.entries.empty()) {
            return decode_fields(structured.fields, analyzer, structured.mode);
        }
    } catch (const Error&) {
        structured_error = std::current_exception();
    }

    if (const auto recovered = recover_fields(file_size, read_at);
        recovered.has_value()) {
        return decode_fields(recovered->fields, analyzer, recovered->mode);
    }
    if (structured_error != nullptr) {
        std::rethrow_exception(structured_error);
    }
    return RiffInfoUtf8Result{
        RiffInfoUtf8Status::no_supported_fields,
        {},
        {},
        RiffInfoReadMode::structured};
}

} // namespace cue_charset::detail
