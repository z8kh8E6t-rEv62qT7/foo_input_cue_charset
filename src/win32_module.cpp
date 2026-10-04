#include "win32_module.hpp"

#include <utility>

#include "cue_charset/cue_charset.hpp"

namespace cue_charset::detail {
namespace {

std::string path_as_utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

} // namespace

Win32Module::Win32Module(const std::filesystem::path& path) : path_(path) {
    handle_ = ::LoadLibraryExW(
        path_.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (handle_ == nullptr) {
        const auto code = ::GetLastError();
        throw Error(
            "unable to load ICU DLL '" + path_as_utf8(path_) +
            "' (Windows error " + std::to_string(code) + ")");
    }
}

Win32Module::~Win32Module() {
    if (handle_ != nullptr) {
        ::FreeLibrary(handle_);
    }
}

Win32Module::Win32Module(Win32Module&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)), path_(std::move(other.path_)) {}

Win32Module& Win32Module::operator=(Win32Module&& other) noexcept {
    if (this != &other) {
        if (handle_ != nullptr) {
            ::FreeLibrary(handle_);
        }
        handle_ = std::exchange(other.handle_, nullptr);
        path_ = std::move(other.path_);
    }
    return *this;
}

FARPROC Win32Module::find(const std::string_view symbol) const {
    const std::string terminated(symbol);
    const auto address = ::GetProcAddress(handle_, terminated.c_str());
    if (address == nullptr) {
        throw Error(
            "required ICU 78 symbol '" + terminated + "' is missing from '" +
            path_as_utf8(path_) + "'");
    }
    return address;
}

} // namespace cue_charset::detail
