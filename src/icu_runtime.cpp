#include "icu_runtime.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cue_charset::detail {
namespace {

template <typename Function>
Function load_function(const Win32Module& module, const std::string_view name) {
    return reinterpret_cast<Function>(module.find(name));
}

const char* byte_data(const std::span<const std::byte> input) {
    static constexpr char empty_input = '\0';
    return input.empty() ? &empty_input : reinterpret_cast<const char*>(input.data());
}

int32_t checked_length(const std::span<const std::byte> input) {
    if (input.size() > static_cast<std::size_t>(std::numeric_limits<int32_t>::max())) {
        throw Error("input is too large for the ICU 77 C API");
    }
    return static_cast<int32_t>(input.size());
}

struct DetectorCloser {
    void (*close)(UCharsetDetector*) = nullptr;
    void operator()(UCharsetDetector* detector) const noexcept {
        if (detector != nullptr) {
            close(detector);
        }
    }
};

struct ConverterCloser {
    void (*close)(UConverter*) = nullptr;
    void operator()(UConverter* converter) const noexcept {
        if (converter != nullptr) {
            close(converter);
        }
    }
};

struct ReplacementContext {
    const char* input_begin = nullptr;
    const char* input_end = nullptr;
    void (*write_uchars)(
        UConverterToUnicodeArgs*, const UChar*, int32_t, int32_t, UErrorCode*) = nullptr;
    std::size_t count = 0;
    std::optional<std::size_t> first_offset;
};

std::optional<std::size_t> source_offset(
    const ReplacementContext& context,
    const UConverterToUnicodeArgs* args,
    const char* code_units,
    const int32_t length) {
    const auto begin = reinterpret_cast<std::uintptr_t>(context.input_begin);
    const auto end = reinterpret_cast<std::uintptr_t>(context.input_end);
    const auto direct = reinterpret_cast<std::uintptr_t>(code_units);
    if (direct >= begin && direct < end) {
        return static_cast<std::size_t>(direct - begin);
    }

    const auto current = reinterpret_cast<std::uintptr_t>(args->source);
    const auto consumed = static_cast<std::uintptr_t>(std::max(length, 0));
    if (current >= begin + consumed && current <= end) {
        return static_cast<std::size_t>(current - consumed - begin);
    }
    return std::nullopt;
}

void U_CALLCONV replacement_callback(
    const void* raw_context,
    UConverterToUnicodeArgs* args,
    const char* code_units,
    const int32_t length,
    const UConverterCallbackReason reason,
    UErrorCode* status) {
    if (reason != UCNV_UNASSIGNED && reason != UCNV_ILLEGAL && reason != UCNV_IRREGULAR) {
        return;
    }

    auto& context = *static_cast<ReplacementContext*>(const_cast<void*>(raw_context));
    ++context.count;
    if (!context.first_offset.has_value()) {
        context.first_offset = source_offset(context, args, code_units, length);
    }

    constexpr UChar replacement = 0xFFFD;
    *status = U_ZERO_ERROR;
    context.write_uchars(args, &replacement, 1, 0, status);
}

} // namespace

IcuRuntime::IcuRuntime(const std::filesystem::path& root)
    : libcxx_(root / "bin" / "libc++.dll"),
      data_(root / "bin" / "libicudt77.dll"),
      common_(root / "bin" / "libicuuc77.dll"),
      i18n_(root / "bin" / "libicuin77.dll") {
    detect_signature_ = load_function<DetectSignatureFn>(common_, "ucnv_detectUnicodeSignature_77");
    detector_open_ = load_function<DetectorOpenFn>(i18n_, "ucsdet_open_77");
    detector_close_ = load_function<DetectorCloseFn>(i18n_, "ucsdet_close_77");
    detector_set_text_ = load_function<DetectorSetTextFn>(i18n_, "ucsdet_setText_77");
    detector_detect_all_ =
        load_function<DetectorDetectAllFn>(i18n_, "ucsdet_detectAll_77");
    detector_get_name_ = load_function<DetectorGetNameFn>(i18n_, "ucsdet_getName_77");
    detector_get_confidence_ =
        load_function<DetectorGetConfidenceFn>(i18n_, "ucsdet_getConfidence_77");
    converter_open_ = load_function<ConverterOpenFn>(common_, "ucnv_open_77");
    converter_close_ = load_function<ConverterCloseFn>(common_, "ucnv_close_77");
    set_to_callback_ = load_function<SetToCallbackFn>(common_, "ucnv_setToUCallBack_77");
    to_unicode_ = load_function<ToUnicodeFn>(common_, "ucnv_toUnicode_77");
    write_uchars_ = load_function<WriteUCharsFn>(common_, "ucnv_cbToUWriteUChars_77");
    string_to_utf8_ = load_function<StringToUtf8Fn>(common_, "u_strToUTF8_77");
    error_name_ = load_function<ErrorNameFn>(common_, "u_errorName_77");
}

DetectionResult IcuRuntime::detect(const std::span<const std::byte> input) const {
    return detect_candidates(input).front();
}

std::vector<DetectionResult> IcuRuntime::detect_candidates(
    const std::span<const std::byte> input) const {
    if (input.empty()) {
        throw Error("cannot detect the charset of an empty file");
    }

    const auto length = checked_length(input);
    UErrorCode status = U_ZERO_ERROR;
    int32_t signature_length = 0;
    const char* signature =
        detect_signature_(byte_data(input), length, &signature_length, &status);
    if (U_FAILURE(status)) {
        throw Error("ICU Unicode signature detection failed: " + error_text(status));
    }
    if (signature != nullptr) {
        return {DetectionResult{
            std::string(signature),
            static_cast<std::size_t>(signature_length),
            std::string(signature),
            std::nullopt}};
    }

    status = U_ZERO_ERROR;
    std::unique_ptr<UCharsetDetector, DetectorCloser> detector(
        detector_open_(&status), DetectorCloser{detector_close_});
    if (U_FAILURE(status) || detector == nullptr) {
        throw Error("ICU charset detector creation failed: " + error_text(status));
    }

    detector_set_text_(detector.get(), byte_data(input), length, &status);
    if (U_FAILURE(status)) {
        throw Error("ICU charset detector input failed: " + error_text(status));
    }

    int32_t match_count = 0;
    const UCharsetMatch** matches =
        detector_detect_all_(detector.get(), &match_count, &status);
    if (U_FAILURE(status) || matches == nullptr || match_count <= 0) {
        throw Error("ICU did not produce charset candidates: " + error_text(status));
    }

    std::vector<DetectionResult> results;
    results.reserve(static_cast<std::size_t>(match_count));
    for (int32_t index = 0; index < match_count; ++index) {
        const UCharsetMatch* match = matches[index];
        if (match == nullptr) {
            throw Error("ICU returned a null charset candidate");
        }

        status = U_ZERO_ERROR;
        const char* encoding = detector_get_name_(match, &status);
        if (U_FAILURE(status) || encoding == nullptr || *encoding == '\0') {
            throw Error("ICU charset candidate has no encoding name: " + error_text(status));
        }

        status = U_ZERO_ERROR;
        const auto confidence = detector_get_confidence_(match, &status);
        if (U_FAILURE(status)) {
            throw Error("ICU charset confidence lookup failed: " + error_text(status));
        }

        results.push_back(DetectionResult{std::nullopt, 0, encoding, confidence});
    }

    return results;
}

ConversionResult IcuRuntime::convert(
    const std::span<const std::byte> input,
    const std::string_view encoding) const {
    if (encoding.empty()) {
        throw Error("cannot convert with an empty charset name");
    }
    checked_length(input);

    ReplacementContext replacement{
        byte_data(input), byte_data(input) + input.size(), write_uchars_, 0, std::nullopt};
    const std::string terminated_encoding(encoding);
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<UConverter, ConverterCloser> converter(
        converter_open_(terminated_encoding.c_str(), &status), ConverterCloser{converter_close_});
    if (U_FAILURE(status) || converter == nullptr) {
        throw Error(
            "ICU could not open converter '" + terminated_encoding + "': " + error_text(status));
    }

    set_to_callback_(
        converter.get(), replacement_callback, &replacement, nullptr, nullptr, &status);
    if (U_FAILURE(status)) {
        throw Error("ICU could not install the replacement callback: " + error_text(status));
    }

    const char* source = byte_data(input);
    const char* const source_limit = source + input.size();
    std::vector<UChar> utf16;
    utf16.reserve(input.size());
    std::array<UChar, 16 * 1024> chunk{};

    for (;;) {
        UChar* target = chunk.data();
        status = U_ZERO_ERROR;
        to_unicode_(
            converter.get(), &target, chunk.data() + chunk.size(), &source, source_limit, nullptr,
            true, &status);
        utf16.insert(utf16.end(), chunk.data(), target);

        if (status == U_BUFFER_OVERFLOW_ERROR) {
            continue;
        }
        if (U_FAILURE(status)) {
            throw Error(
                "ICU conversion from '" + terminated_encoding + "' failed: " +
                error_text(status));
        }
        if (source == source_limit) {
            break;
        }
    }

    if (utf16.size() > static_cast<std::size_t>(std::numeric_limits<int32_t>::max())) {
        throw Error("converted UTF-16 text is too large for the ICU 77 C API");
    }

    int32_t utf8_length = 0;
    status = U_ZERO_ERROR;
    string_to_utf8_(
        nullptr, 0, &utf8_length, utf16.data(), static_cast<int32_t>(utf16.size()), &status);
    if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status)) {
        throw Error("ICU UTF-8 size calculation failed: " + error_text(status));
    }

    std::string utf8(static_cast<std::size_t>(utf8_length), '\0');
    status = U_ZERO_ERROR;
    string_to_utf8_(
        utf8.empty() ? nullptr : utf8.data(), utf8_length, &utf8_length,
        utf16.empty() ? nullptr : utf16.data(),
        static_cast<int32_t>(utf16.size()), &status);
    if (U_FAILURE(status)) {
        throw Error("ICU UTF-8 conversion failed: " + error_text(status));
    }
    utf8.resize(static_cast<std::size_t>(utf8_length));

    return ConversionResult{
        std::move(utf8), replacement.count, replacement.first_offset};
}

std::string IcuRuntime::error_text(const UErrorCode status) const {
    const char* name = error_name_(status);
    return name != nullptr ? std::string(name) : "UErrorCode " + std::to_string(status);
}

} // namespace cue_charset::detail
