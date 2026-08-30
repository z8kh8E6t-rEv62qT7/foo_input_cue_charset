#include "component_diagnostics.hpp"

#include <string>

namespace cue_charset::foobar_component {

namespace {

void log_diagnostic(
    const char* severity,
    const std::string_view stage,
    const std::string_view event,
    const char* details) {
    FB2K_console_formatter()
        << "CUE Charset Input "
        << severity
        << " ["
        << std::string(stage).c_str()
        << "/"
        << std::string(event).c_str()
        << "]: "
        << details;
}

} // namespace

void log_warning(
    const std::string_view stage,
    const std::string_view event,
    const char* details) {
    log_diagnostic("warning", stage, event, details);
}

void log_failure(
    const std::string_view stage,
    const std::string_view event,
    const std::exception& error) {
    log_diagnostic(
        "error",
        stage,
        event,
        (PFC_string_formatter() << "failed: " << error.what()).c_str());
}

} // namespace cue_charset::foobar_component
