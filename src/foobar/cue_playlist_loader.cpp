#include "foobar_sdk.hpp"

#include <foobar2000/SDK/file_info_impl.h>
#include <foobar2000/SDK/playlist_loader.h>

#include <exception>
#include <memory>

#include "component_diagnostics.hpp"
#include "cue_document.hpp"
#include "cue_encoding.hpp"

namespace cue_charset::foobar_component {
namespace {

class CueCharsetPlaylistLoader : public playlist_loader {
public:
    void open(
        const char* path,
        const file::ptr& source,
        playlist_loader_callback::ptr callback,
        abort_callback& abort) override {
        std::unique_ptr<CueDocument> document;
        std::optional<std::uint32_t> current_track;
        const char* event = "open";
        try {
            document = CueDocument::load(source, path, abort);
            event = "cue-file-stats";
            const auto stats = document->get_stats2(stats2_all, abort);
            for (const auto& track : document->tracks()) {
                current_track = track.number;
                event = "entry-create";
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
                    event = "track-info";
                    document->get_info(track.number, info, abort);
                    event = "entry-submit";
                    callback->on_entry_info(
                        handle,
                        playlist_loader_callback::entry_from_playlist,
                        legacy_stats,
                        info,
                        true);
                } else {
                    event = "entry-submit";
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
            log_failure("loader", event, error,
                document ? document->diagnostic_context(current_track) :
                    "cue=" + detail::quote_diagnostic_value(path));
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
