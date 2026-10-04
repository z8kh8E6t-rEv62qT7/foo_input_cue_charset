#include "foobar_sdk.hpp"

DECLARE_COMPONENT_VERSION(
    "CUE Charset Input",
    "0.3.5",
    "Read-only external CUE playlist loader and input with ICU 78 character-set detection and configurable encoding priority (Preferences > Tools > CUE Charset Input), plus optional UTF-8 RIFF INFO and FLAC duration repair filters.");

VALIDATE_COMPONENT_FILENAME("foo_input_cue_charset.dll");

FOOBAR2000_IMPLEMENT_CFG_VAR_DOWNGRADE;
