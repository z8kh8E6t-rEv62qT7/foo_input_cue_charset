#pragma once

#include <filesystem>
#include <string_view>

#include <windows.h>

namespace cue_charset::detail {

class Win32Module final {
public:
    Win32Module() = default;
    explicit Win32Module(const std::filesystem::path& path);
    ~Win32Module();

    Win32Module(Win32Module&& other) noexcept;
    Win32Module& operator=(Win32Module&& other) noexcept;
    Win32Module(const Win32Module&) = delete;
    Win32Module& operator=(const Win32Module&) = delete;

    [[nodiscard]] FARPROC find(std::string_view symbol) const;

private:
    HMODULE handle_ = nullptr;
    std::filesystem::path path_;
};

} // namespace cue_charset::detail

