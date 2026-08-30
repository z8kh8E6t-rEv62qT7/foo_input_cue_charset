#include "foobar_sdk.hpp"

#include <foobar2000/SDK/file_info_impl.h>
#include <foobar2000/SDK/playlist_loader.h>

#include <exception>
#include <memory>

#include "component_diagnostics.hpp"
#include "cue_document.hpp"

namespace cue_charset::foobar_component {
namespace {

class CueCharsetPlaylistLoader : public playlist_loader {
public:
    void open(
        const char* path,
        const file::ptr& source,
        playlist_loader_callback::ptr callback,
        abort_callback& abort) override {
        try {
            const auto document = CueDocument::load(source, path, abort);
            const auto stats = document->get_stats2(stats2_all, abort);
            for (const auto& track : document->tracks()) {
                abort.check();
                const auto location = detail::make_cue_subsong_location(
                    document->cue_path().c_str(), track);
                metadb_handle_ptr handle;
                callback->handle_create(
                    handle,
                    make_playable_location(
                        location.cue_path.c_str(),
                        location.subsong));
                const auto legacy_stats = stats.as_legacy();
                if (callback->want_info(
                        handle,
                        playlist_loader_callback::entry_from_playlist,
                        legacy_stats,
                        true)) {
                    file_info_impl info;
                    document->get_info(track.number, info, abort);
                    callback->on_entry_info(
                        handle,
                        playlist_loader_callback::entry_from_playlist,
                        legacy_stats,
                        info,
                        true);
                } else {
                    callback->on_entry(
                        handle,
                        playlist_loader_callback::entry_from_playlist,
                        legacy_stats,
                        true);
                }
            }
        } catch (const exception_aborted&) {
            throw;
        } catch (const std::exception& error) {
            log_failure("loader", "open", error);
            throw;
        }
    }

    void write(
        const char*,
        const file::ptr&,
        metadb_handle_list_cref,
        abort_callback&) override {
        throw pfc::exception_not_implemented();
    }

    const char* get_extension() override {
        return "cue";
    }

    bool can_write() override {
        return false;
    }

    bool is_our_content_type(const char*) override {
        return false;
    }

    bool is_associatable() override {
        return false;
    }
};

static playlist_loader_factory_t<CueCharsetPlaylistLoader> g_cue_charset_playlist_loader;

} // namespace
} // namespace cue_charset::foobar_component
