#include "riff_info_repair.hpp"

#include "cue_charset/cue_charset.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace cue_charset::foobar_component {
namespace {

const char* metadata_name(const detail::RiffInfoField field) noexcept {
    switch (field) {
    case detail::RiffInfoField::title:
        return "TITLE";
    case detail::RiffInfoField::artist:
        return "ARTIST";
    case detail::RiffInfoField::album:
        return "ALBUM";
    case detail::RiffInfoField::track_number:
        return "TRACKNUMBER";
    case detail::RiffInfoField::date:
        return "DATE";
    case detail::RiffInfoField::genre:
        return "GENRE";
    case detail::RiffInfoField::comment:
        return "COMMENT";
    case detail::RiffInfoField::encoder:
        return "ENCODER";
    }
    return nullptr;
}

} // namespace

detail::RiffInfoUtf8Result load_riff_info_utf8_repair(
    const char* const path,
    const Analyzer& analyzer,
    abort_callback& abort) {
    if (path == nullptr || *path == '\0') {
        throw Error("referenced audio path is empty");
    }

    file::ptr source;
    filesystem::g_open_read(source, path, abort);
    if (!source->can_seek()) {
        throw Error("referenced audio stream is not seekable");
    }

    const t_filesize reported_size = source->get_size(abort);
    if (reported_size == filesize_invalid) {
        throw Error("referenced audio size is unknown");
    }
    const auto file_size = static_cast<std::uint64_t>(reported_size);

    const detail::RiffReadAt read_at =
        [source, file_size, &abort](
            const std::uint64_t offset,
            const std::span<std::byte> output) {
            if (offset > file_size ||
                output.size() > file_size - offset) {
                throw Error("RIFF read exceeds the referenced audio boundary");
            }
            source->seek(static_cast<t_filesize>(offset), abort);
            if (!output.empty()) {
                source->read_object(output.data(), output.size(), abort);
            }
        };

    return detail::read_riff_info_utf8(file_size, read_at, analyzer);
}

void apply_riff_info_utf8_repair(
    const detail::RiffInfoUtf8Result& repair,
    file_info& info) {
    if (repair.status != detail::RiffInfoUtf8Status::applied) {
        return;
    }
    for (const auto& tag : repair.tags) {
        const char* const name = metadata_name(tag.field);
        if (name != nullptr) {
            info.meta_set(name, tag.value.c_str());
        }
    }
}

} // namespace cue_charset::foobar_component
