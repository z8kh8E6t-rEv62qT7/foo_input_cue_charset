#include "foobar_sdk.hpp"

#include <foobar2000/helpers/input_helper_cue.h>
#include <foobar2000/SDK/input_impl.h>

#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <utility>

#include "component_diagnostics.hpp"
#include "cue_document.hpp"

namespace cue_charset::foobar_component {
namespace {

class CueCharsetInput final : public input_stubs {
public:
    void open(
        file::ptr file_hint,
        const char* path,
        const t_input_open_reason reason,
        abort_callback& abort) {
        if (reason == input_open_info_write) {
            throw exception_tagging_unsupported();
        }
        try {
            document_ = CueDocument::load(std::move(file_hint), path, abort);
        } catch (const exception_aborted&) {
            throw;
        } catch (const std::exception& error) {
            log_failure("decoder", "open", error);
            throw;
        }
    }

    unsigned get_subsong_count() const {
        const auto count = document_->tracks().size();
        if (count > std::numeric_limits<unsigned>::max()) {
            fail("CUE sheet contains too many tracks");
        }
        return static_cast<unsigned>(count);
    }

    t_uint32 get_subsong(const unsigned index) const {
        if (index >= document_->tracks().size()) {
            throw exception_io_bad_subsong_index();
        }
        return document_->tracks()[index].number;
    }

    void get_info(
        const t_uint32 subsong,
        file_info& info,
        abort_callback& abort) const {
        document_->get_info(subsong, info, abort);
    }

    t_filestats2 get_stats2(
        const std::uint32_t flags,
        abort_callback& abort) const {
        return document_->get_stats2(flags, abort);
    }

    void decode_initialize(
        const t_uint32 subsong,
        const unsigned flags,
        abort_callback& abort) {
        const auto& selected = document_->track(subsong);
        if (decoder_.is_open()) {
            decoder_.close();
        }
        unsigned effective_flags = flags & ~input_flag_allow_inaccurate_seeking;
        if (selected.start_seconds > 0) {
            effective_flags &= ~input_flag_no_seeking;
        }
        const double length = selected.known_length_seconds.value_or(0.0);
        try {
            decoder_.open(
                nullptr,
                make_playable_location(selected.referenced_path.c_str(), 0),
                effective_flags,
                abort,
                selected.start_seconds,
                length,
                selected.binary);
        } catch (const exception_aborted&) {
            throw;
        } catch (const std::exception& error) {
            log_failure("decode", "initialize", error);
            throw;
        }
        if (logger_.is_valid()) {
            decoder_.set_logger(logger_);
        }
    }

    bool decode_run(audio_chunk& chunk, abort_callback& abort) {
        try {
            return decoder_.run(chunk, abort);
        } catch (const exception_aborted&) {
            throw;
        } catch (const std::exception& error) {
            log_failure("decode", "run", error);
            throw;
        }
    }

    bool decode_run_raw(
        audio_chunk& chunk,
        mem_block_container& raw,
        abort_callback& abort) {
        try {
            return decoder_.run_raw(chunk, raw, abort);
        } catch (const exception_aborted&) {
            throw;
        } catch (const std::exception& error) {
            log_failure("decode", "run-raw", error);
            throw;
        }
    }

    void decode_seek(const double seconds, abort_callback& abort) {
        try {
            decoder_.seek(seconds, abort);
        } catch (const exception_aborted&) {
            throw;
        } catch (const std::exception& error) {
            log_failure("decode", "seek", error);
            throw;
        }
    }

    bool decode_can_seek() const {
        return true;
    }

    bool decode_get_dynamic_info(file_info& info, double& timestamp_delta) {
        try {
            return decoder_.get_dynamic_info(info, timestamp_delta);
        } catch (const std::exception& error) {
            log_failure("decode", "dynamic-info", error);
            throw;
        }
    }

    bool decode_get_dynamic_info_track(file_info& info, double& timestamp_delta) {
        try {
            return decoder_.get_dynamic_info_track(info, timestamp_delta);
        } catch (const std::exception& error) {
            log_failure("decode", "dynamic-info-track", error);
            throw;
        }
    }

    void decode_on_idle(abort_callback& abort) {
        try {
            decoder_.on_idle(abort);
        } catch (const exception_aborted&) {
            throw;
        } catch (const std::exception& error) {
            log_failure("decode", "idle", error);
            throw;
        }
    }

    void set_logger(event_logger::ptr logger) {
        logger_ = std::move(logger);
    }

    void retag_set_info(t_uint32, const file_info&, abort_callback&) {
        throw exception_tagging_unsupported();
    }

    void retag_commit(abort_callback&) {
        throw exception_tagging_unsupported();
    }

    void remove_tags(abort_callback&) {
        throw exception_tagging_unsupported();
    }

    static bool g_is_our_content_type(const char*) {
        return false;
    }

    static bool g_is_our_path(const char*, const char* extension) {
        return extension != nullptr && pfc::stricmp_ascii(extension, "cue") == 0;
    }

    static const char* g_get_name() {
        return "CUE Charset Input";
    }

    static GUID g_get_guid() {
        return {
            0xfed89855,
            0x6c1f,
            0x4e28,
            {0x82, 0x06, 0xb7, 0x9d, 0xc9, 0x79, 0x77, 0xb3}};
    }

private:
    std::unique_ptr<CueDocument> document_;
    input_helper_cue decoder_;
    event_logger::ptr logger_;
};

static input_factory_t<CueCharsetInput> g_cue_charset_factory;

} // namespace
} // namespace cue_charset::foobar_component
