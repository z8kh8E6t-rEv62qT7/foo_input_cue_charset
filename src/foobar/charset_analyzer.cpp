#include "charset_analyzer.hpp"

namespace cue_charset::foobar_component {

const Analyzer& shared_charset_analyzer() {
    static const Analyzer instance;
    return instance;
}

} // namespace cue_charset::foobar_component
