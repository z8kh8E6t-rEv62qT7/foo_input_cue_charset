#include "cue_charset/cue_charset.hpp"

#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

#include "config.hpp"
#include "icu_runtime.hpp"

namespace cue_charset {
namespace {

std::string path_as_utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

std::vector<std::byte> read_file(const std::filesystem::path& path, std::uintmax_t& byte_count) {
    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    if (error) {
        throw Error(
            "cannot inspect input file '" + path_as_utf8(path) +
            "' (filesystem error " + std::to_string(error.value()) + ")");
    }
    if (!std::filesystem::is_regular_file(status)) {
        throw Error("input path is not a regular file: '" + path_as_utf8(path) + "'");
    }

    byte_count = std::filesystem::file_size(path, error);
    if (error) {
        throw Error(
            "cannot determine input size for '" + path_as_utf8(path) +
            "' (filesystem error " + std::to_string(error.value()) + ")");
    }
    if (byte_count == 0) {
        throw Error("input file is empty: '" + path_as_utf8(path) + "'");
    }
    if (byte_count > max_file_size) {
        throw Error(
            "input file exceeds the 128 MiB limit: '" + path_as_utf8(path) + "'");
    }
    if (byte_count > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        throw Error("input file cannot be represented by std::streamsize");
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw Error("cannot open input file for reading: '" + path_as_utf8(path) + "'");
    }

    std::vector<std::byte> bytes(static_cast<std::size_t>(byte_count));
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
        throw Error("short read while reading input file: '" + path_as_utf8(path) + "'");
    }
    if (stream.peek() != std::ifstream::traits_type::eof()) {
        throw Error("input file changed size while it was being read: '" + path_as_utf8(path) + "'");
    }
    return bytes;
}

} // namespace

class Analyzer::Impl final {
public:
    explicit Impl(std::filesystem::path icu_root) : runtime(std::move(icu_root)) {}
    detail::IcuRuntime runtime;
};

Analyzer::Analyzer() : Analyzer(default_icu_root()) {}

Analyzer::Analyzer(std::filesystem::path icu_root)
    : impl_(std::make_unique<Impl>(std::move(icu_root))) {}

Analyzer::~Analyzer() = default;
Analyzer::Analyzer(Analyzer&&) noexcept = default;
Analyzer& Analyzer::operator=(Analyzer&&) noexcept = default;

AnalysisResult Analyzer::analyze_file(const std::filesystem::path& path) const {
    std::error_code error;
    auto absolute = std::filesystem::absolute(path, error);
    if (error) {
        absolute = path;
    }
    absolute = absolute.lexically_normal();

    std::uintmax_t byte_count = 0;
    const auto bytes = read_file(absolute, byte_count);
    auto detection = detect(bytes);
    auto conversion = convert(bytes, detection.encoding);
    return AnalysisResult{
        std::move(absolute), byte_count, std::move(detection), std::move(conversion)};
}

DetectionResult Analyzer::detect(const std::span<const std::byte> input) const {
    return impl_->runtime.detect(input);
}

std::vector<DetectionResult> Analyzer::detect_candidates(
    const std::span<const std::byte> input) const {
    return impl_->runtime.detect_candidates(input);
}

ConversionResult Analyzer::convert(
    const std::span<const std::byte> input,
    const std::string_view encoding) const {
    return impl_->runtime.convert(input, encoding);
}

std::filesystem::path default_icu_root() {
    return std::filesystem::path{CUE_CHARSET_DEFAULT_ICU_ROOT};
}

} // namespace cue_charset
