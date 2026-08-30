#include "riff_info_tests.hpp"

#include "cue_charset/cue_charset.hpp"
#include "riff_info.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cue_charset::tests {
namespace {

using Bytes = std::vector<std::byte>;
using Field = std::pair<std::array<char, 4>, Bytes>;
inline constexpr auto max_chunk_size =
    std::numeric_limits<std::uint32_t>::max();

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Callable>
void require_error(Callable&& callable, const std::string_view expected) {
    try {
        std::invoke(std::forward<Callable>(callable));
    } catch (const Error& error) {
        require(
            std::string_view(error.what()).find(expected) != std::string_view::npos,
            std::string("unexpected RIFF error: ") + error.what());
        return;
    }
    throw std::runtime_error("expected RIFF parser error was not thrown");
}

Bytes bytes(const std::string_view text) {
    const auto* const begin =
        reinterpret_cast<const std::byte*>(text.data());
    return {begin, begin + text.size()};
}

void append_fourcc(Bytes& output, const std::array<char, 4>& id) {
    for (const char value : id) {
        output.push_back(static_cast<std::byte>(value));
    }
}

void append_u32(Bytes& output, const std::uint32_t value) {
    output.push_back(static_cast<std::byte>(value & 0xFFU));
    output.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
    output.push_back(static_cast<std::byte>((value >> 16U) & 0xFFU));
    output.push_back(static_cast<std::byte>((value >> 24U) & 0xFFU));
}

void set_u32(Bytes& output, const std::size_t offset, const std::uint32_t value) {
    require(offset + 4 <= output.size(), "test RIFF patch offset is invalid");
    for (std::size_t index = 0; index < 4; ++index) {
        output[offset + index] =
            static_cast<std::byte>((value >> (8U * index)) & 0xFFU);
    }
}

void append_chunk(
    Bytes& output,
    const std::array<char, 4>& id,
    const std::span<const std::byte> payload) {
    require(
        payload.size() <= max_chunk_size,
        "test RIFF chunk does not fit a 32-bit size");
    append_fourcc(output, id);
    append_u32(output, static_cast<std::uint32_t>(payload.size()));
    output.insert(output.end(), payload.begin(), payload.end());
    if ((payload.size() & 1U) != 0) {
        output.push_back(std::byte{0});
    }
}

Bytes make_wave(const std::vector<Field>& fields) {
    Bytes wave;
    append_fourcc(wave, {'R', 'I', 'F', 'F'});
    append_u32(wave, 0);
    append_fourcc(wave, {'W', 'A', 'V', 'E'});

    Bytes info_payload;
    append_fourcc(info_payload, {'I', 'N', 'F', 'O'});
    for (const auto& [id, value] : fields) {
        append_chunk(info_payload, id, value);
    }
    append_chunk(wave, {'L', 'I', 'S', 'T'}, info_payload);
    require(wave.size() - 8 <= max_chunk_size, "test RIFF is too large");
    set_u32(wave, 4, static_cast<std::uint32_t>(wave.size() - 8));
    return wave;
}

Bytes make_info_list(const std::vector<Field>& fields) {
    Bytes info_payload;
    append_fourcc(info_payload, {'I', 'N', 'F', 'O'});
    for (const auto& [id, value] : fields) {
        append_chunk(info_payload, id, value);
    }
    Bytes list;
    append_chunk(list, {'L', 'I', 'S', 'T'}, info_payload);
    return list;
}

void finalize_riff_size(Bytes& wave) {
    require(wave.size() >= 12, "test RIFF is too small");
    require(wave.size() - 8 <= max_chunk_size, "test RIFF is too large");
    set_u32(wave, 4, static_cast<std::uint32_t>(wave.size() - 8));
}

Bytes make_broken_wave_prefix() {
    Bytes wave;
    append_fourcc(wave, {'R', 'I', 'F', 'F'});
    append_u32(wave, 0);
    append_fourcc(wave, {'W', 'A', 'V', 'E'});
    append_fourcc(wave, {'J', 'U', 'N', 'K'});
    append_u32(wave, max_chunk_size);
    return wave;
}

detail::RiffInfoUtf8Result parse(
    const Bytes& input,
    const Analyzer& analyzer) {
    const detail::RiffReadAt read_at =
        [&input](const std::uint64_t offset, const std::span<std::byte> output) {
            if (offset > input.size() || output.size() > input.size() - offset) {
                throw Error("test reader boundary exceeded");
            }
            std::copy_n(
                input.begin() + static_cast<std::ptrdiff_t>(offset),
                output.size(),
                output.begin());
        };
    return detail::read_riff_info_utf8(input.size(), read_at, analyzer);
}

const std::string* find_tag(
    const detail::RiffInfoUtf8Result& result,
    const detail::RiffInfoField field) {
    const auto found = std::find_if(
        result.tags.begin(), result.tags.end(),
        [field](const detail::RiffInfoTag& tag) { return tag.field == field; });
    return found == result.tags.end() ? nullptr : &found->value;
}

void test_valid_utf8(const Analyzer& analyzer) {
    const auto wave = make_wave({
        {{'I', 'N', 'A', 'M'}, bytes("\xE5\xA4\x8F\xE5\xBD\xB1")},
        {{'I', 'A', 'R', 'T'}, bytes("KEY")},
        {{'I', 'P', 'R', 'D'}, bytes("Air Original Soundtrack (disc 1)")},
        {{'I', 'T', 'R', 'K'}, bytes("01")},
        {{'I', 'C', 'R', 'D'}, bytes("2002")},
        {{'I', 'G', 'N', 'R'}, bytes("Soundtrack")},
        {{'I', 'C', 'M', 'T'}, bytes("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E")},
        {{'I', 'S', 'F', 'T'}, bytes("encoder")}});
    const auto result = parse(wave, analyzer);
    require(
        result.status == detail::RiffInfoUtf8Status::applied,
        "valid non-ASCII UTF-8 RIFF INFO was not applied");
    require(
        result.mode == detail::RiffInfoReadMode::structured,
        "valid RIFF INFO did not use structured parsing");
    require(
        find_tag(result, detail::RiffInfoField::title) != nullptr &&
            *find_tag(result, detail::RiffInfoField::title) ==
                "\xE5\xA4\x8F\xE5\xBD\xB1",
        "RIFF INAM did not map to TITLE");
    require(
        find_tag(result, detail::RiffInfoField::track_number) != nullptr &&
            *find_tag(result, detail::RiffInfoField::track_number) == "01",
        "RIFF ITRK did not map to TRACKNUMBER");
    require(result.tags.size() == 8, "supported RIFF INFO field count changed");
}

void test_ascii_and_unsupported(const Analyzer& analyzer) {
    const auto ascii = parse(
        make_wave({{{'I', 'N', 'A', 'M'}, bytes("ASCII title")}}),
        analyzer);
    require(
        ascii.status == detail::RiffInfoUtf8Status::ascii_only &&
            ascii.tags.empty(),
        "ASCII-only RIFF INFO unexpectedly overrode native tags");

    const auto unknown = parse(
        make_wave({{{'I', 'X', 'X', 'X'}, bytes("\xE5\xA4\x8F")}}),
        analyzer);
    require(
        unknown.status == detail::RiffInfoUtf8Status::no_supported_fields,
        "unknown RIFF INFO FourCC was treated as a supported tag");

    Bytes rf64 = make_wave({{{'I', 'N', 'A', 'M'}, bytes("\xE5\xA4\x8F")}});
    const Bytes rf64_id = bytes("RF64");
    std::copy(rf64_id.begin(), rf64_id.end(), rf64.begin());
    const auto unsupported_container = parse(rf64, analyzer);
    require(
        unsupported_container.status == detail::RiffInfoUtf8Status::not_riff_wave,
        "RF64 container was accepted as classic RIFF/WAVE");

    const auto require_unsupported_signature =
        [&analyzer](
            const std::string_view name,
            const std::span<const std::byte> signature) {
            Bytes input(12, std::byte{0});
            require(
                signature.size() <= input.size(),
                "unsupported-format test signature is too large");
            std::copy(signature.begin(), signature.end(), input.begin());
            const auto result = parse(input, analyzer);
            require(
                result.status == detail::RiffInfoUtf8Status::not_riff_wave &&
                    result.tags.empty(),
                std::string(name) +
                    " signature was accepted as classic RIFF/WAVE");
        };

    const Bytes rifx = bytes("RIFX");
    const Bytes wave64 = bytes("riff");
    const Bytes aiff = bytes("FORM");
    const Bytes flac = bytes("fLaC");
    const Bytes ape = bytes("MAC ");
    const Bytes apev2 = bytes("APETAGEX");
    const Bytes wavpack = bytes("wvpk");
    const Bytes id3 = bytes("ID3");
    const Bytes tak = bytes("tBaK");
    const Bytes tta = bytes("TTA1");
    const Bytes mp4{
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0x18},
        std::byte{'f'}, std::byte{'t'}, std::byte{'y'}, std::byte{'p'}};
    require_unsupported_signature("RIFX", rifx);
    require_unsupported_signature("WAVE64", wave64);
    require_unsupported_signature("AIFF", aiff);
    require_unsupported_signature("FLAC", flac);
    require_unsupported_signature("Monkey's Audio", ape);
    require_unsupported_signature("APEv2", apev2);
    require_unsupported_signature("WavPack", wavpack);
    require_unsupported_signature("ID3", id3);
    require_unsupported_signature("TAK", tak);
    require_unsupported_signature("TTA", tta);
    require_unsupported_signature("MP4", mp4);

    Bytes riff_non_wave = bytes("RIFF");
    append_u32(riff_non_wave, 4);
    append_fourcc(riff_non_wave, {'A', 'V', 'I', ' '});
    const auto non_wave_result = parse(riff_non_wave, analyzer);
    require(
        non_wave_result.status == detail::RiffInfoUtf8Status::not_riff_wave &&
            non_wave_result.tags.empty(),
        "non-WAVE RIFF container was accepted for INFO repair");
}

void test_atomic_validation(const Analyzer& analyzer) {
    const Bytes invalid_utf8{std::byte{0xC3}, std::byte{0x28}};
    const auto result = parse(
        make_wave({
            {{'I', 'N', 'A', 'M'}, bytes("\xE5\xA4\x8F")},
            {{'I', 'A', 'R', 'T'}, invalid_utf8}}),
        analyzer);
    require(
        result.status == detail::RiffInfoUtf8Status::invalid_text &&
            result.tags.empty(),
        "one invalid UTF-8 field did not reject the whole RIFF INFO repair");

    Bytes valid_nul = bytes("\xE5\xA4\x8F");
    valid_nul.insert(valid_nul.end(), {std::byte{0}, std::byte{0}});
    const auto nul_result = parse(
        make_wave({{{'I', 'N', 'A', 'M'}, valid_nul}}),
        analyzer);
    require(
        nul_result.status == detail::RiffInfoUtf8Status::applied,
        "legal trailing RIFF INFO NUL bytes were rejected");

    Bytes invalid_nul = bytes("\xE5\xA4\x8F");
    invalid_nul.insert(
        invalid_nul.end(),
        {std::byte{0}, std::byte{'x'}});
    const auto invalid_nul_result = parse(
        make_wave({{{'I', 'N', 'A', 'M'}, invalid_nul}}),
        analyzer);
    require(
        invalid_nul_result.status == detail::RiffInfoUtf8Status::invalid_text,
        "non-zero bytes after a NUL terminator were accepted");
}

void test_duplicates_track_fallback_and_padding(const Analyzer& analyzer) {
    const auto result = parse(
        make_wave({
            {{'I', 'N', 'A', 'M'}, bytes("\xE5\xA4\x8F")},
            {{'I', 'N', 'A', 'M'}, bytes("second")},
            {{'I', 'P', 'R', 'T'}, bytes("7")},
            {{'I', 'X', 'X', 'X'}, bytes("ignored")}}),
        analyzer);
    require(
        result.status == detail::RiffInfoUtf8Status::applied,
        "odd-sized INFO payload or padding was parsed incorrectly");
    require(
        find_tag(result, detail::RiffInfoField::title) != nullptr &&
            *find_tag(result, detail::RiffInfoField::title) ==
                "\xE5\xA4\x8F",
        "duplicate RIFF INFO field did not keep its first value");
    require(
        find_tag(result, detail::RiffInfoField::track_number) != nullptr &&
            *find_tag(result, detail::RiffInfoField::track_number) == "7",
        "IPRT did not provide the ITRK fallback");

    const auto preferred_track = parse(
        make_wave({
            {{'I', 'N', 'A', 'M'}, bytes("\xE5\xA4\x8F")},
            {{'I', 'P', 'R', 'T'}, bytes("7")},
            {{'I', 'T', 'R', 'K'}, bytes("8")}}),
        analyzer);
    require(
        find_tag(preferred_track, detail::RiffInfoField::track_number) != nullptr &&
            *find_tag(preferred_track, detail::RiffInfoField::track_number) == "8",
        "ITRK did not take precedence over IPRT");
}

void test_truncated_data_tolerance(const Analyzer& analyzer) {
    Bytes wave = make_wave({
        {{'I', 'N', 'A', 'M'}, bytes("\xE5\xA4\x8F\xE5\xBD\xB1")},
        {{'I', 'A', 'R', 'T'}, bytes("KEY")}});
    append_fourcc(wave, {'d', 'a', 't', 'a'});
    append_u32(wave, 6);
    wave.insert(
        wave.end(),
        {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}});
    finalize_riff_size(wave);

    const auto result = parse(wave, analyzer);
    require(
        result.status == detail::RiffInfoUtf8Status::applied &&
            result.mode == detail::RiffInfoReadMode::structured_truncated_data,
        "final data overstatement did not preserve preceding RIFF INFO");
    require(
        find_tag(result, detail::RiffInfoField::title) != nullptr &&
            *find_tag(result, detail::RiffInfoField::title) ==
                "\xE5\xA4\x8F\xE5\xBD\xB1",
        "truncated-data recovery returned the wrong title");
}

void test_bounded_recovery(const Analyzer& analyzer) {
    Bytes head = make_broken_wave_prefix();
    const Bytes head_list = make_info_list(
        {{{'I', 'N', 'A', 'M'}, bytes("\xE5\xA4\x8F")}});
    head.insert(head.end(), head_list.begin(), head_list.end());
    finalize_riff_size(head);
    const auto head_result = parse(head, analyzer);
    require(
        head_result.status == detail::RiffInfoUtf8Status::applied &&
            head_result.mode == detail::RiffInfoReadMode::recovery_head,
        "bounded head recovery did not find a valid INFO list");

    Bytes tail = make_broken_wave_prefix();
    tail.resize(
        static_cast<std::size_t>(detail::riff_info_recovery_window_size + 1024),
        std::byte{0});
    const Bytes tail_list = make_info_list(
        {{{'I', 'N', 'A', 'M'}, bytes("\xE5\xB0\xBE")}});
    tail.insert(tail.end(), tail_list.begin(), tail_list.end());
    finalize_riff_size(tail);
    const auto tail_result = parse(tail, analyzer);
    require(
        tail_result.status == detail::RiffInfoUtf8Status::applied &&
            tail_result.mode == detail::RiffInfoReadMode::recovery_tail,
        "bounded tail recovery did not find a valid INFO list");

    Bytes middle = make_broken_wave_prefix();
    middle.resize(
        static_cast<std::size_t>(detail::riff_info_recovery_window_size + 1024),
        std::byte{0});
    const Bytes middle_list = make_info_list(
        {{{'I', 'N', 'A', 'M'}, bytes("\xE4\xB8\xAD")}});
    middle.insert(middle.end(), middle_list.begin(), middle_list.end());
    middle.resize(
        static_cast<std::size_t>(2 * detail::riff_info_recovery_window_size + 4096),
        std::byte{0});
    finalize_riff_size(middle);
    require_error(
        [&] { (void)parse(middle, analyzer); },
        "top-level RIFF chunk exceeds the RIFF boundary");
}

void test_recovery_candidate_validation(const Analyzer& analyzer) {
    Bytes wave = make_broken_wave_prefix();
    append_fourcc(wave, {'L', 'I', 'S', 'T'});
    append_u32(wave, 8);
    append_fourcc(wave, {'I', 'N', 'F', 'O'});
    append_fourcc(wave, {'I', 'N', 'A', 'M'});
    finalize_riff_size(wave);
    require_error(
        [&] { (void)parse(wave, analyzer); },
        "top-level RIFF chunk exceeds the RIFF boundary");

    Bytes atomic = make_broken_wave_prefix();
    const Bytes valid = make_info_list(
        {{{'I', 'N', 'A', 'M'}, bytes("\xE5\xA4\x8F")}});
    atomic.insert(atomic.end(), valid.begin(), valid.end());
    atomic.resize(
        static_cast<std::size_t>(detail::riff_info_recovery_window_size + 1024),
        std::byte{0});
    const Bytes invalid_utf8{std::byte{0xC3}, std::byte{0x28}};
    const Bytes invalid = make_info_list(
        {{{'I', 'A', 'R', 'T'}, invalid_utf8}});
    atomic.insert(atomic.end(), invalid.begin(), invalid.end());
    finalize_riff_size(atomic);
    const auto atomic_result = parse(atomic, analyzer);
    require(
        atomic_result.status == detail::RiffInfoUtf8Status::invalid_text &&
            atomic_result.tags.empty(),
        "invalid UTF-8 in a second recovered INFO list was not atomic");
}

void test_malformed_boundaries(const Analyzer& analyzer) {
    Bytes truncated = make_wave(
        {{{'I', 'N', 'A', 'M'}, bytes("\xE5\xA4\x8F")}});
    truncated.pop_back();
    require_error(
        [&] { (void)parse(truncated, analyzer); },
        "top-level RIFF chunk exceeds the RIFF boundary");

    Bytes huge_chunk;
    append_fourcc(huge_chunk, {'R', 'I', 'F', 'F'});
    append_u32(huge_chunk, max_chunk_size);
    append_fourcc(huge_chunk, {'W', 'A', 'V', 'E'});
    append_fourcc(huge_chunk, {'J', 'U', 'N', 'K'});
    append_u32(huge_chunk, max_chunk_size);
    const std::uint64_t virtual_size =
        static_cast<std::uint64_t>(max_chunk_size) + 8U;
    const detail::RiffReadAt sparse_read =
        [&huge_chunk](const std::uint64_t offset, const std::span<std::byte> output) {
            std::fill(output.begin(), output.end(), std::byte{0});
            if (offset >= huge_chunk.size()) {
                return;
            }
            const std::size_t available = static_cast<std::size_t>(
                std::min<std::uint64_t>(output.size(), huge_chunk.size() - offset));
            std::copy_n(
                huge_chunk.begin() + static_cast<std::ptrdiff_t>(offset),
                available, output.begin());
        };
    require_error(
        [&] {
            (void)detail::read_riff_info_utf8(
                virtual_size, sparse_read, analyzer);
        },
        "exceeds the RIFF boundary");
}

void test_size_limits(const Analyzer& analyzer) {
    Bytes oversized_field(
        static_cast<std::size_t>(detail::max_riff_info_field_size + 1),
        static_cast<std::byte>('x'));
    require_error(
        [&] {
            (void)parse(
                make_wave({{{'I', 'N', 'A', 'M'}, oversized_field}}),
                analyzer);
        },
        "1 MiB");

    const Bytes one_megabyte(
        static_cast<std::size_t>(detail::max_riff_info_field_size),
        static_cast<std::byte>('x'));
    require_error(
        [&] {
            (void)parse(
                make_wave({
                    {{'I', 'N', 'A', 'M'}, one_megabyte},
                    {{'I', 'A', 'R', 'T'}, one_megabyte},
                    {{'I', 'P', 'R', 'D'}, one_megabyte},
                    {{'I', 'C', 'R', 'D'}, one_megabyte},
                    {{'I', 'G', 'N', 'R'}, one_megabyte}}),
                analyzer);
        },
        "4 MiB");
}

} // namespace

void run_riff_info_tests(const Analyzer& analyzer) {
    test_valid_utf8(analyzer);
    test_ascii_and_unsupported(analyzer);
    test_atomic_validation(analyzer);
    test_duplicates_track_fallback_and_padding(analyzer);
    test_truncated_data_tolerance(analyzer);
    test_bounded_recovery(analyzer);
    test_recovery_candidate_validation(analyzer);
    test_malformed_boundaries(analyzer);
    test_size_limits(analyzer);
}

} // namespace cue_charset::tests
