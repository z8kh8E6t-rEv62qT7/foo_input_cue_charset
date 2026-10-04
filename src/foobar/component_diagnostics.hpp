#pragma once

#include "foobar_sdk.hpp"

#include <exception>
#include <string_view>

namespace cue_charset::foobar_component {

void log_warning(std::string_view stage, std::string_view event, const char* details);
void log_failure(std::string_view stage, std::string_view event, const std::exception& error,
    std::string_view context = {});

} // namespace cue_charset::foobar_component
