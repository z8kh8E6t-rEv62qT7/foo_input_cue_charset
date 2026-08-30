#include "foobar_sdk.hpp"

DECLARE_COMPONENT_VERSION(
    "CUE Charset Input",
    "0.3.4",
    "Read-only external CUE playlist loader and input with ICU 77 character-set detection, plus an optional UTF-8 RIFF INFO filter for direct WAVE files.");

VALIDATE_COMPONENT_FILENAME("foo_input_cue_charset.dll");

FOOBAR2000_IMPLEMENT_CFG_VAR_DOWNGRADE;
