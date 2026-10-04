#pragma once

#include <string>
#include <vector>

namespace cue_charset::foobar_component {

// Each load takes one thread-safe snapshot of the persisted global setting.
[[nodiscard]] std::vector<std::string> configured_encoding_priority();

} // namespace cue_charset::foobar_component
