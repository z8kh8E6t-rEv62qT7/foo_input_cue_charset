#pragma once

#include "foobar_sdk.hpp"

#include "riff_info.hpp"

namespace cue_charset {

class Analyzer;

namespace foobar_component {

[[nodiscard]] detail::RiffInfoUtf8Result load_riff_info_utf8_repair(
    const char* path,
    const Analyzer& analyzer,
    abort_callback& abort);

void apply_riff_info_utf8_repair(
    const detail::RiffInfoUtf8Result& repair,
    file_info& info);

} // namespace foobar_component
} // namespace cue_charset
