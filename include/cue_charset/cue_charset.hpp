#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace cue_charset {

inline constexpr std::uintmax_t max_file_size = 128ULL * 1024ULL * 1024ULL;

class Error final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct DetectionResult {
    std::optional<std::string> unicode_signature;
    std::size_t signature_length = 0;
    std::string encoding;
    std::optional<int> confidence;
};

struct ConversionResult {
    std::string utf8;
    std::size_t replacement_count = 0;
    std::optional<std::size_t> first_replacement_offset;
};

struct AnalysisResult {
    std::filesystem::path path;
    std::uintmax_t byte_count = 0;
    DetectionResult detection;
    ConversionResult conversion;
};

class Analyzer final {
public:
    Analyzer();
    explicit Analyzer(std::filesystem::path icu_root);
    ~Analyzer();

    Analyzer(Analyzer&&) noexcept;
    Analyzer& operator=(Analyzer&&) noexcept;
    Analyzer(const Analyzer&) = delete;
    Analyzer& operator=(const Analyzer&) = delete;

    [[nodiscard]] AnalysisResult analyze_file(const std::filesystem::path& path) const;
    [[nodiscard]] DetectionResult detect(std::span<const std::byte> input) const;
    [[nodiscard]] std::vector<DetectionResult> detect_candidates(
        std::span<const std::byte> input) const;
    [[nodiscard]] ConversionResult convert(
        std::span<const std::byte> input,
        std::string_view encoding) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::filesystem::path default_icu_root();
[[nodiscard]] std::string format_diagnostics(const AnalysisResult& result);

} // namespace cue_charset
