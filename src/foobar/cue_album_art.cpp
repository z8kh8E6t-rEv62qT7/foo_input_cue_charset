#include "foobar_sdk.hpp"

#include <foobar2000/SDK/album_art_helpers.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "component_diagnostics.hpp"
#include "cue_document.hpp"

namespace cue_charset::foobar_component {
namespace {

constexpr t_filesize maximum_external_art_size = 128ULL * 1024ULL * 1024ULL;

std::string ascii_lower(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    return value;
}

std::string path_to_utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {
        reinterpret_cast<const char*>(value.data()),
        value.size()};
}

std::filesystem::path path_from_utf8(const std::string_view value) {
    const auto* begin = reinterpret_cast<const char8_t*>(value.data());
    return std::filesystem::path(std::u8string(begin, begin + value.size()));
}

std::filesystem::path native_parent(const char* canonical_path) {
    pfc::string8 native;
    if (!foobar2000_io::extract_native_path(canonical_path, native)) {
        return {};
    }
    return path_from_utf8(native.c_str()).parent_path();
}

std::string find_external_front_cover(
    const char* referenced_path,
    const char* cue_path) {
    const std::array directories{
        native_parent(referenced_path),
        native_parent(cue_path)};
    constexpr std::array preferred_stems{"cover", "folder", "front"};

    std::vector<std::filesystem::path> candidates;
    for (std::size_t directory_index = 0; directory_index < directories.size(); ++directory_index) {
        const auto& directory = directories[directory_index];
        if (directory.empty() ||
            (directory_index != 0 && directory == directories.front())) {
            continue;
        }
        std::error_code error;
        for (std::filesystem::directory_iterator iterator(directory, error), end;
             !error && iterator != end;
             iterator.increment(error)) {
            if (!iterator->is_regular_file(error)) {
                error.clear();
                continue;
            }
            const auto stem = ascii_lower(path_to_utf8(iterator->path().stem()));
            if (std::find(preferred_stems.begin(), preferred_stems.end(), stem) !=
                preferred_stems.end()) {
                candidates.push_back(iterator->path());
            }
        }
        if (error) {
            log_warning(
                "art",
                "directory-scan",
                (PFC_string_formatter()
                 << "skipped: " << path_to_utf8(directory).c_str()
                 << ", error=" << error.message().c_str())
                    .c_str());
        }
        for (const auto preferred : preferred_stems) {
            std::sort(candidates.begin(), candidates.end());
            const auto found = std::find_if(
                candidates.begin(),
                candidates.end(),
                [preferred](const std::filesystem::path& path) {
                    return ascii_lower(path_to_utf8(path.stem())) == preferred;
                });
            if (found != candidates.end()) {
                pfc::string8 canonical;
                const auto utf8 = path_to_utf8(*found);
                filesystem::g_get_canonical_path(utf8.c_str(), canonical);
                return canonical.c_str();
            }
        }
        candidates.clear();
    }
    return {};
}

class ForwardedAlbumArtInstance : public album_art_extractor_instance_v2 {
public:
    ForwardedAlbumArtInstance(
        album_art_extractor_instance_ptr embedded,
        std::string referenced_path,
        std::string external_front_path)
        : embedded_(std::move(embedded)),
          referenced_path_(std::move(referenced_path)),
          external_front_path_(std::move(external_front_path)) {}

    album_art_data_ptr query(const GUID& what, abort_callback& abort) override {
        if (embedded_.is_valid()) {
            try {
                return embedded_->query(what, abort);
            } catch (const exception_album_art_not_found&) {
            }
        }
        if (what != album_art_ids::cover_front || external_front_path_.empty()) {
            throw exception_album_art_not_found();
        }
        if (external_front_cache_.is_empty()) {
            file::ptr source;
            filesystem::g_open_read(source, external_front_path_.c_str(), abort);
            const t_filesize size = source->get_size_ex(abort);
            if (size == 0 || size > maximum_external_art_size) {
                throw exception_album_art_unsupported_format();
            }
            external_front_cache_ = album_art_data_impl::g_create(
                source.get_ptr(), pfc::downcast_guarded<t_size>(size), abort);
        }
        return external_front_cache_;
    }

    album_art_path_list::ptr query_paths(
        const GUID& what,
        abort_callback& abort) override {
        if (embedded_.is_valid()) {
            try {
                (void)embedded_->query(what, abort);
                album_art_extractor_instance_v2::ptr embedded_v2;
                if (embedded_v2 &= embedded_) {
                    return embedded_v2->query_paths(what, abort);
                }
                return fb2k::service_new<album_art_path_list_impl>(
                    referenced_path_.c_str());
            } catch (const exception_album_art_not_found&) {
            }
        }
        if (what == album_art_ids::cover_front && !external_front_path_.empty()) {
            return fb2k::service_new<album_art_path_list_impl>(
                external_front_path_.c_str());
        }
        throw exception_album_art_not_found();
    }

private:
    album_art_extractor_instance_ptr embedded_;
    std::string referenced_path_;
    std::string external_front_path_;
    album_art_data_ptr external_front_cache_;
};

class CueTrackAlbumArtFallback : public album_art_fallback {
public:
    album_art_extractor_instance_v2::ptr open(
        metadb_handle_list_cref items,
        pfc::list_base_const_t<GUID> const&,
        abort_callback& abort) override {
        if (items.get_count() == 0) {
            throw exception_album_art_not_found();
        }
        try {
            std::string common_reference;
            std::string first_cue_path;
            for (t_size index = 0; index < items.get_count(); ++index) {
                abort.check();
                const auto handle = items.get_item(index);
                const char* path = handle->get_path();
                if (pfc::stricmp_ascii(pfc::string_extension(path), "cue") != 0) {
                    throw exception_album_art_not_found();
                }
                const auto document = CueDocument::load(nullptr, path, abort);
                const auto& track = document->track(handle->get_subsong_index());
                if (index == 0) {
                    common_reference = track.referenced_path;
                    first_cue_path = document->cue_path().c_str();
                } else if (
                    metadb::path_compare(
                        common_reference.c_str(),
                        track.referenced_path.c_str()) != 0) {
                    throw exception_album_art_not_found();
                }
            }

            album_art_extractor_instance_ptr embedded;
            try {
                embedded = album_art_extractor::g_open_allowempty(
                    nullptr, common_reference.c_str(), abort);
            } catch (const exception_album_art_not_found&) {
            } catch (const exception_album_art_unsupported_format&) {
            }
            const std::string external = find_external_front_cover(
                common_reference.c_str(), first_cue_path.c_str());
            return fb2k::service_new<ForwardedAlbumArtInstance>(
                embedded,
                common_reference,
                external);
        } catch (const exception_aborted&) {
            throw;
        } catch (const exception_album_art_not_found&) {
            throw;
        } catch (const std::exception& error) {
            log_failure("art", "open", error);
            throw;
        }
    }
};

static service_factory_single_t<CueTrackAlbumArtFallback> g_cue_track_album_art;

} // namespace
} // namespace cue_charset::foobar_component
