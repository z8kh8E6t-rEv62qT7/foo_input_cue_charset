if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

set(removed_files
    "${PROJECT_SOURCE_DIR}/include/cue_charset/virtual_location.hpp"
    "${PROJECT_SOURCE_DIR}/src/virtual_location.cpp"
    "${PROJECT_SOURCE_DIR}/src/foobar/cue_virtual_filesystem.cpp")
foreach(path IN LISTS removed_files)
    if(EXISTS "${path}")
        message(FATAL_ERROR "Removed virtual-location file still exists: ${path}")
    endif()
endforeach()

file(GLOB_RECURSE production_sources
    "${PROJECT_SOURCE_DIR}/include/*.h"
    "${PROJECT_SOURCE_DIR}/include/*.hpp"
    "${PROJECT_SOURCE_DIR}/src/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/*.h"
    "${PROJECT_SOURCE_DIR}/src/*.hpp")

set(forbidden_tokens
    "cuecharset://"
    "VirtualLocation"
    "virtual_location"
    "filesystem_v3"
    "input_singletrack_factory_t"
    "ActiveX"
    "CreateProcess"
    "ShellExecute"
    "URLDownloadToFile"
    "WinHttp"
    "WinINet"
    "log_event"
    "log_aborted")

foreach(path IN LISTS production_sources)
    file(READ "${path}" contents)
    foreach(token IN LISTS forbidden_tokens)
        string(FIND "${contents}" "${token}" found_at)
        if(NOT found_at EQUAL -1)
            message(FATAL_ERROR "Forbidden token '${token}' found in ${path}")
        endif()
    endforeach()
endforeach()

file(READ "${PROJECT_SOURCE_DIR}/src/foobar/component_diagnostics.cpp"
    diagnostics_source)
foreach(required_severity IN ITEMS "warning" "error")
    string(FIND "${diagnostics_source}" "\"${required_severity}\"" severity_at)
    if(severity_at EQUAL -1)
        message(FATAL_ERROR
            "Component diagnostics must identify ${required_severity} messages")
    endif()
endforeach()

file(READ "${PROJECT_SOURCE_DIR}/src/foobar/cue_input.cpp" input_source)
string(FIND "${input_source}" "input_factory_t<CueCharsetInput>" factory_at)
if(factory_at EQUAL -1)
    message(FATAL_ERROR "The standard multitrack input_factory_t registration is missing")
endif()
string(FIND "${input_source}"
    "pfc::stricmp_ascii(extension, \"cue\")" cue_extension_match_at)
if(cue_extension_match_at EQUAL -1)
    message(FATAL_ERROR
        "The decoder path matcher must remain restricted to the CUE extension")
endif()
foreach(forbidden_extension IN ITEMS
        wav wave aif aiff mp3 ape flac wv wavpack mp4 m4a tak tta)
    string(FIND "${input_source}"
        "stricmp_ascii(extension, \"${forbidden_extension}\")"
        forbidden_extension_at)
    if(NOT forbidden_extension_at EQUAL -1)
        message(FATAL_ERROR
            "The decoder must not claim .${forbidden_extension} files")
    endif()
endforeach()

foreach(read_only_method IN ITEMS retag_set_info retag_commit remove_tags)
    string(REGEX MATCH
        "${read_only_method}[^}]*exception_tagging_unsupported"
        read_only_match
        "${input_source}")
    if(read_only_match STREQUAL "")
        message(FATAL_ERROR
            "Read-only method '${read_only_method}' does not reject tag writing")
    endif()
endforeach()

file(READ "${PROJECT_SOURCE_DIR}/src/foobar/cue_playlist_loader.cpp" loader_source)
string(FIND "${loader_source}" "make_cue_subsong_location" loader_mapping_at)
if(loader_mapping_at EQUAL -1)
    message(FATAL_ERROR "Playlist loader does not use the native CUE/subsong mapper")
endif()
string(FIND "${loader_source}" "return \"cue\";" loader_extension_at)
if(loader_extension_at EQUAL -1)
    message(FATAL_ERROR "Playlist loader must remain restricted to .cue files")
endif()

file(READ "${PROJECT_SOURCE_DIR}/src/foobar/cue_document.cpp" document_source)
foreach(required_candidate_token IN ITEMS
        "detect_candidates(bytes)"
        "conversion.replacement_count != 0"
        "filesystem::g_exists"
        "charset-candidate-fallback")
    string(FIND "${document_source}" "${required_candidate_token}" candidate_token_at)
    if(candidate_token_at EQUAL -1)
        message(FATAL_ERROR
            "CUE charset candidate validation is missing: ${required_candidate_token}")
    endif()
endforeach()
string(FIND "${document_source}" "apply_riff_info_utf8_repair" riff_repair_at)
string(FIND "${document_source}" "read_reference_info(selected, info, abort)" native_info_at)
string(FIND "${document_source}" "cue_parser::parse_info" cue_metadata_at)
if(riff_repair_at EQUAL -1)
    message(FATAL_ERROR "Referenced-audio RIFF INFO repair is not connected")
endif()
if(native_info_at EQUAL -1 OR cue_metadata_at EQUAL -1 OR
        NOT native_info_at LESS cue_metadata_at)
    message(FATAL_ERROR
        "Explicit CUE metadata must be applied after RIFF INFO repair")
endif()

file(READ "${PROJECT_SOURCE_DIR}/src/foobar/riff_info_repair.cpp" repair_source)
foreach(metadata_name IN ITEMS
        TITLE ARTIST ALBUM TRACKNUMBER DATE GENRE COMMENT ENCODER)
    string(FIND "${repair_source}" "\"${metadata_name}\"" metadata_at)
    if(metadata_at EQUAL -1)
        message(FATAL_ERROR
            "RIFF INFO repair mapping is missing ${metadata_name}")
    endif()
endforeach()

file(READ "${PROJECT_SOURCE_DIR}/src/foobar/wave_info_filter.cpp" wave_filter_source)
foreach(required_filter_token IN ITEMS
        "class WaveRiffInfoFilter : public input_info_filter"
        "service_factory_single_t<WaveRiffInfoFilter>"
        "CUE Charset RIFF INFO Filter"
        "foobar2000_io::extract_native_path"
        "stricmp_ascii(extension.c_str(), \"wav\")"
        "stricmp_ascii(extension.c_str(), \"wave\")"
        "return preferences_page::guid_input_info_filter"
        "return true;"
        "return false;")
    string(FIND "${wave_filter_source}" "${required_filter_token}" filter_at)
    if(filter_at EQUAL -1)
        message(FATAL_ERROR
            "Direct WAVE input-info filter requirement is missing: ${required_filter_token}")
    endif()
endforeach()

file(READ "${PROJECT_SOURCE_DIR}/src/foobar/cue_document.cpp" document_source)
string(FIND "${document_source}"
    "const ScopedWaveInfoFilterBypass filter_bypass" filter_bypass_at)
if(filter_bypass_at EQUAL -1)
    message(FATAL_ERROR
        "CUE referenced-audio reads must bypass the global direct-WAVE filter")
endif()

file(READ "${PROJECT_SOURCE_DIR}/src/riff_info.cpp" riff_source)
foreach(required_guard IN ITEMS
        "riff_id = fourcc('R', 'I', 'F', 'F')"
        "wave_id = fourcc('W', 'A', 'V', 'E')"
        "read_u32_le(header.first<4>()) != riff_id"
        "read_u32_le(header.subspan<8, 4>()) != wave_id")
    string(FIND "${riff_source}" "${required_guard}" guard_at)
    if(guard_at EQUAL -1)
        message(FATAL_ERROR
            "Classic RIFF/WAVE scope guard is missing: ${required_guard}")
    endif()
endforeach()

foreach(required_recovery IN ITEMS
        "riff_info_recovery_window_size"
        "structured_truncated_data"
        "recovery_head"
        "recovery_tail"
        "id == data_id")
    string(FIND "${riff_source}" "${required_recovery}" recovery_at)
    if(recovery_at EQUAL -1)
        message(FATAL_ERROR
            "Bounded RIFF recovery behavior is missing: ${required_recovery}")
    endif()
endforeach()
