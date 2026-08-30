#include "cue_charset/cue_charset.hpp"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

bool is_cue_path(const std::filesystem::path& path) {
    auto extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](const wchar_t value) {
        return static_cast<wchar_t>(std::towlower(value));
    });
    return extension == L".cue";
}

} // namespace

int wmain(const int argc, wchar_t* argv[]) {
    if (argc != 2) {
        std::cerr << "usage: cue-charset <file.cue>\n";
        return 1;
    }

    const std::filesystem::path path(argv[1]);
    if (!is_cue_path(path)) {
        std::cerr << "error=input path must have a .cue extension\n";
        return 1;
    }

    try {
        const cue_charset::Analyzer analyzer;
        const auto result = analyzer.analyze_file(path);
        std::cout << cue_charset::format_diagnostics(result);
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "conversion=failure\nerror=" << exception.what() << '\n';
        return 1;
    }
}

