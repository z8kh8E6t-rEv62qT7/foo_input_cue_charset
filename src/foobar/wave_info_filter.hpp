#pragma once

#include "foobar_sdk.hpp"

namespace cue_charset::foobar_component {

inline constexpr GUID wave_riff_info_filter_guid = {
    0x9ac66a33,
    0x3e76,
    0x48a7,
    {0xbd, 0xea, 0xd5, 0x16, 0xc6, 0x5d, 0x12, 0x8a}};

class ScopedWaveInfoFilterBypass final {
public:
    ScopedWaveInfoFilterBypass() noexcept;
    ~ScopedWaveInfoFilterBypass();

    ScopedWaveInfoFilterBypass(const ScopedWaveInfoFilterBypass&) = delete;
    ScopedWaveInfoFilterBypass& operator=(const ScopedWaveInfoFilterBypass&) = delete;
};

[[nodiscard]] bool wave_info_filter_is_bypassed() noexcept;

} // namespace cue_charset::foobar_component
