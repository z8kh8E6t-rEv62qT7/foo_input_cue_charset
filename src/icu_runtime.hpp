#pragma once

#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

#include "cue_charset/cue_charset.hpp"
#include "win32_module.hpp"

#include <unicode/ucnv.h>
#include <unicode/ucsdet.h>
#include <unicode/ustring.h>

namespace cue_charset::detail {

class IcuRuntime final {
public:
    explicit IcuRuntime(const std::filesystem::path& root);

    [[nodiscard]] DetectionResult detect(std::span<const std::byte> input) const;
    [[nodiscard]] std::optional<DetectionResult> detect_unicode_signature(
        std::span<const std::byte> input) const;
    [[nodiscard]] std::vector<DetectionResult> detect_candidates(
        std::span<const std::byte> input) const;
    [[nodiscard]] ConversionResult convert(
        std::span<const std::byte> input,
        std::string_view encoding) const;

private:
    using DetectSignatureFn = const char* (*)(
        const char*, int32_t, int32_t*, UErrorCode*);
    using DetectorOpenFn = UCharsetDetector* (*)(UErrorCode*);
    using DetectorCloseFn = void (*)(UCharsetDetector*);
    using DetectorSetTextFn = void (*)(UCharsetDetector*, const char*, int32_t, UErrorCode*);
    using DetectorDetectAllFn = const UCharsetMatch** (*)(
        UCharsetDetector*, int32_t*, UErrorCode*);
    using DetectorGetNameFn = const char* (*)(const UCharsetMatch*, UErrorCode*);
    using DetectorGetConfidenceFn = int32_t (*)(const UCharsetMatch*, UErrorCode*);
    using ConverterOpenFn = UConverter* (*)(const char*, UErrorCode*);
    using ConverterCloseFn = void (*)(UConverter*);
    using SetToCallbackFn = void (*)(
        UConverter*, UConverterToUCallback, const void*, UConverterToUCallback*,
        const void**, UErrorCode*);
    using ToUnicodeFn = void (*)(
        UConverter*, UChar**, const UChar*, const char**, const char*, int32_t*, UBool,
        UErrorCode*);
    using WriteUCharsFn = void (*)(
        UConverterToUnicodeArgs*, const UChar*, int32_t, int32_t, UErrorCode*);
    using StringToUtf8Fn = char* (*)(
        char*, int32_t, int32_t*, const UChar*, int32_t, UErrorCode*);
    using ErrorNameFn = const char* (*)(UErrorCode);

    Win32Module libcxx_;
    Win32Module data_;
    Win32Module common_;
    Win32Module i18n_;

    DetectSignatureFn detect_signature_ = nullptr;
    DetectorOpenFn detector_open_ = nullptr;
    DetectorCloseFn detector_close_ = nullptr;
    DetectorSetTextFn detector_set_text_ = nullptr;
    DetectorDetectAllFn detector_detect_all_ = nullptr;
    DetectorGetNameFn detector_get_name_ = nullptr;
    DetectorGetConfidenceFn detector_get_confidence_ = nullptr;
    ConverterOpenFn converter_open_ = nullptr;
    ConverterCloseFn converter_close_ = nullptr;
    SetToCallbackFn set_to_callback_ = nullptr;
    ToUnicodeFn to_unicode_ = nullptr;
    WriteUCharsFn write_uchars_ = nullptr;
    StringToUtf8Fn string_to_utf8_ = nullptr;
    ErrorNameFn error_name_ = nullptr;

    [[nodiscard]] std::string error_text(UErrorCode status) const;
};

} // namespace cue_charset::detail
