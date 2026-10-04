cmake_minimum_required(VERSION 3.24)

project(foo_input_cue_charset VERSION 0.3.5 LANGUAGES CXX)

include(CTest)

set(ICU_ROOT "E:/MSYS/clang64" CACHE PATH "Root of the fixed ICU 78 installation")
cmake_path(ABSOLUTE_PATH ICU_ROOT NORMALIZE OUTPUT_VARIABLE ICU_ROOT_ABSOLUTE)

set(ICU_VERSION_HEADER "${ICU_ROOT_ABSOLUTE}/include/unicode/uvernum.h")
if(NOT EXISTS "${ICU_VERSION_HEADER}")
    message(FATAL_ERROR "ICU headers were not found below ICU_ROOT: ${ICU_ROOT_ABSOLUTE}")
endif()

file(STRINGS "${ICU_VERSION_HEADER}" ICU_MAJOR_LINE
    REGEX "^#define[ \t]+U_ICU_VERSION_MAJOR_NUM[ \t]+[0-9]+")
if(NOT ICU_MAJOR_LINE MATCHES "U_ICU_VERSION_MAJOR_NUM[ \t]+78$")
    message(FATAL_ERROR "This prototype requires ICU 78 headers; found: ${ICU_MAJOR_LINE}")
endif()

# The MSYS ICU prefix also contains MinGW C runtime headers. Giving that whole
# include root to MSVC would shadow the MSVC runtime headers, so stage only ICU's
# self-contained unicode/ tree in the build directory.
set(ICU_HEADER_STAGE "${CMAKE_CURRENT_BINARY_DIR}/icu-include")
file(MAKE_DIRECTORY "${ICU_HEADER_STAGE}")
file(COPY "${ICU_ROOT_ABSOLUTE}/include/unicode" DESTINATION "${ICU_HEADER_STAGE}")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/config.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/config.hpp"
    @ONLY
)

add_library(cue_charset STATIC
    src/analyzer.cpp
    src/cue_reference_path.cpp
    src/cue_encoding.cpp
    src/diagnostics.cpp
    src/flac_duration_repair.cpp
    src/icu_runtime.cpp
    src/riff_info.cpp
    src/track_layout.cpp
    src/win32_module.cpp
)
add_library(cue_charset::cue_charset ALIAS cue_charset)

target_include_directories(cue_charset
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_include_directories(cue_charset SYSTEM PRIVATE "${ICU_HEADER_STAGE}")
target_compile_features(cue_charset PUBLIC cxx_std_20)
target_compile_definitions(cue_charset PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

add_executable(cue-charset src/main.cpp)
target_link_libraries(cue-charset PRIVATE cue_charset::cue_charset)
target_compile_features(cue-charset PRIVATE cxx_std_20)

if(MSVC)
    foreach(target cue_charset cue-charset)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /EHsc /utf-8)
    endforeach()
endif()

option(BUILD_FOOBAR2000_COMPONENT "Build the x64 foobar2000 input component" ON)
set(FOOBAR2000_SDK_ROOT "D:/Foobar2000/sdk" CACHE PATH "foobar2000 SDK root")
set(
    WTL_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/deps/WTL10_01_Release"
    CACHE PATH
    "WTL 10.01 root required by the official foobar2000 helpers project")
set(
    VISUAL_STUDIO_ROOT
    "D:/Microsoft Visual Studio"
    CACHE PATH
    "Visual Studio installation root")
set(
    MSBUILD_EXECUTABLE
    "${VISUAL_STUDIO_ROOT}/MSBuild/Current/Bin/amd64/MSBuild.exe"
    CACHE FILEPATH
    "MSBuild used for official foobar2000 SDK projects")
set(
    WINDOWS_SDK_ROOT
    "C:/Program Files (x86)/Windows Kits/10"
    CACHE PATH
    "Windows SDK root for MSBuild")
set(WINDOWS_SDK_VERSION "10.0.26100.0" CACHE STRING "Windows SDK version for MSBuild")
set(
    MSVC_TOOLS_ROOT
    "${VISUAL_STUDIO_ROOT}/VC/Tools/MSVC/14.44.35207"
    CACHE PATH
    "MSVC toolset root for official SDK projects")
set(MSVC_TOOLS_VERSION "14.44.35207" CACHE STRING "MSVC toolset version")

if(BUILD_FOOBAR2000_COMPONENT)
    enable_language(RC)
    if(NOT MSVC OR NOT WIN32)
        message(FATAL_ERROR "The foobar2000 component requires MSVC on Windows")
    endif()
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "The foobar2000 component is x64-only")
    endif()

    cmake_path(
        ABSOLUTE_PATH FOOBAR2000_SDK_ROOT
        NORMALIZE OUTPUT_VARIABLE FOOBAR2000_SDK_ROOT_ABSOLUTE)
    if(NOT EXISTS "${FOOBAR2000_SDK_ROOT_ABSOLUTE}/foobar2000/SDK/foobar2000_SDK.vcxproj")
        message(FATAL_ERROR "Official foobar2000 SDK not found: ${FOOBAR2000_SDK_ROOT_ABSOLUTE}")
    endif()
    if(NOT EXISTS "${MSBUILD_EXECUTABLE}")
        message(FATAL_ERROR "MSBuild not found: ${MSBUILD_EXECUTABLE}")
    endif()
    if(NOT EXISTS "${WINDOWS_SDK_ROOT}/Include/${WINDOWS_SDK_VERSION}/shared/SDKDDKVer.h")
        message(FATAL_ERROR
            "Windows SDK ${WINDOWS_SDK_VERSION} not found below: ${WINDOWS_SDK_ROOT}")
    endif()
    if(NOT EXISTS "${MSVC_TOOLS_ROOT}/include/vcruntime.h")
        message(FATAL_ERROR "MSVC standard library headers not found: ${MSVC_TOOLS_ROOT}")
    endif()
    if(NOT EXISTS "${MSVC_TOOLS_ROOT}/atlmfc/include/atlbase.h")
        message(FATAL_ERROR "ATL headers not found for the selected MSVC toolset: ${MSVC_TOOLS_ROOT}")
    endif()
    cmake_path(ABSOLUTE_PATH WTL_ROOT NORMALIZE OUTPUT_VARIABLE WTL_ROOT_ABSOLUTE)
    set(WTL_INCLUDE_DIR "${WTL_ROOT_ABSOLUTE}/Include")
    if(NOT EXISTS "${WTL_INCLUDE_DIR}/atlapp.h")
        message(FATAL_ERROR "WTL 10.01 headers not found below: ${WTL_ROOT_ABSOLUTE}")
    endif()

    set(FOOBAR_MSBUILD_COMMON_ARGS
        /nologo /m /verbosity:minimal /t:Build
        /p:UseEnv=true
        /p:Platform=x64
        /p:PlatformToolset=v143
        "/p:VCToolsInstallDir=${MSVC_TOOLS_ROOT}/"
        "/p:VCToolsVersion=${MSVC_TOOLS_VERSION}"
        "/p:VCInstallDir=${VISUAL_STUDIO_ROOT}/VC/"
        "/p:VSInstallDir=${VISUAL_STUDIO_ROOT}/"
        "/p:WindowsSdkDir_10=${WINDOWS_SDK_ROOT}/"
        "/p:WindowsSdkDir=${WINDOWS_SDK_ROOT}/"
        "/p:UniversalCRTSdkDir_10=${WINDOWS_SDK_ROOT}/"
        "/p:UniversalCRTSdkDir=${WINDOWS_SDK_ROOT}/"
        "/p:WindowsTargetPlatformVersion=${WINDOWS_SDK_VERSION}"
        "/p:WindowsSDKVersion=${WINDOWS_SDK_VERSION}/"
        "/p:UCRTVersion=${WINDOWS_SDK_VERSION}")
    set(FOOBAR_MSBUILD_PATH
        "${MSVC_TOOLS_ROOT}/bin/Hostx64/x64;${WINDOWS_SDK_ROOT}/bin/${WINDOWS_SDK_VERSION}/x64;$ENV{PATH}")
    set(FOOBAR_MSBUILD_INCLUDE
        "${WTL_INCLUDE_DIR};${MSVC_TOOLS_ROOT}/include;${MSVC_TOOLS_ROOT}/atlmfc/include;${WINDOWS_SDK_ROOT}/Include/${WINDOWS_SDK_VERSION}/ucrt;${WINDOWS_SDK_ROOT}/Include/${WINDOWS_SDK_VERSION}/shared;${WINDOWS_SDK_ROOT}/Include/${WINDOWS_SDK_VERSION}/um;${WINDOWS_SDK_ROOT}/Include/${WINDOWS_SDK_VERSION}/winrt;${WINDOWS_SDK_ROOT}/Include/${WINDOWS_SDK_VERSION}/cppwinrt")
    set(FOOBAR_MSBUILD_LIB
        "${MSVC_TOOLS_ROOT}/lib/x64;${MSVC_TOOLS_ROOT}/atlmfc/lib/x64;${WINDOWS_SDK_ROOT}/Lib/${WINDOWS_SDK_VERSION}/ucrt/x64;${WINDOWS_SDK_ROOT}/Lib/${WINDOWS_SDK_VERSION}/um/x64")
    string(REPLACE ";" "\\;" FOOBAR_MSBUILD_PATH_ESCAPED "${FOOBAR_MSBUILD_PATH}")
    string(REPLACE ";" "\\;" FOOBAR_MSBUILD_INCLUDE_ESCAPED "${FOOBAR_MSBUILD_INCLUDE}")
    string(REPLACE ";" "\\;" FOOBAR_MSBUILD_LIB_ESCAPED "${FOOBAR_MSBUILD_LIB}")
    set(FOOBAR_MSBUILD_COMMAND
        "${CMAKE_COMMAND}" -E env
        "PATH=${FOOBAR_MSBUILD_PATH_ESCAPED}"
        "INCLUDE=${FOOBAR_MSBUILD_INCLUDE_ESCAPED}"
        "LIB=${FOOBAR_MSBUILD_LIB_ESCAPED}"
        "${MSBUILD_EXECUTABLE}")

    set(FOOBAR_SDK_LIB_DIR "${CMAKE_CURRENT_BINARY_DIR}/foobar-sdk/lib")
    set(FOOBAR_SDK_OBJ_DIR "${CMAKE_CURRENT_BINARY_DIR}/foobar-sdk/obj")
    set(PFC_LIBRARY "${FOOBAR_SDK_LIB_DIR}/pfc.lib")
    set(FOOBAR_SDK_LIBRARY "${FOOBAR_SDK_LIB_DIR}/foobar2000_SDK.lib")
    set(FOOBAR_COMPONENT_CLIENT_LIBRARY
        "${FOOBAR_SDK_LIB_DIR}/foobar2000_component_client.lib")
    set(FOOBAR_HELPERS_LIBRARY
        "${FOOBAR_SDK_LIB_DIR}/foobar2000_sdk_helpers.lib")
    set(FOOBAR_UI_LIBRARY "${FOOBAR_SDK_LIB_DIR}/libPPUI.lib")

    add_custom_target(foobar2000_sdk_build
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${FOOBAR_SDK_LIB_DIR}"
            "${FOOBAR_SDK_OBJ_DIR}/pfc"
            "${FOOBAR_SDK_OBJ_DIR}/sdk"
            "${FOOBAR_SDK_OBJ_DIR}/component-client"
            "${FOOBAR_SDK_OBJ_DIR}/helpers"
            "${FOOBAR_SDK_OBJ_DIR}/ui"
        COMMAND ${FOOBAR_MSBUILD_COMMAND}
            "${FOOBAR2000_SDK_ROOT_ABSOLUTE}/pfc/pfc.vcxproj"
            ${FOOBAR_MSBUILD_COMMON_ARGS}
            "/p:Configuration=Release FB2K"
            "/p:OutDir=${FOOBAR_SDK_LIB_DIR}/"
            "/p:IntDir=${FOOBAR_SDK_OBJ_DIR}/pfc/"
        COMMAND ${FOOBAR_MSBUILD_COMMAND}
            "${FOOBAR2000_SDK_ROOT_ABSOLUTE}/foobar2000/SDK/foobar2000_SDK.vcxproj"
            ${FOOBAR_MSBUILD_COMMON_ARGS}
            /p:Configuration=Release
            "/p:OutDir=${FOOBAR_SDK_LIB_DIR}/"
            "/p:IntDir=${FOOBAR_SDK_OBJ_DIR}/sdk/"
        COMMAND ${FOOBAR_MSBUILD_COMMAND}
            "${FOOBAR2000_SDK_ROOT_ABSOLUTE}/foobar2000/foobar2000_component_client/foobar2000_component_client.vcxproj"
            ${FOOBAR_MSBUILD_COMMON_ARGS}
            /p:Configuration=Release
            "/p:OutDir=${FOOBAR_SDK_LIB_DIR}/"
            "/p:IntDir=${FOOBAR_SDK_OBJ_DIR}/component-client/"
        COMMAND ${FOOBAR_MSBUILD_COMMAND}
            "${FOOBAR2000_SDK_ROOT_ABSOLUTE}/foobar2000/helpers/foobar2000_sdk_helpers.vcxproj"
            ${FOOBAR_MSBUILD_COMMON_ARGS}
            /p:Configuration=Release
            "/p:OutDir=${FOOBAR_SDK_LIB_DIR}/"
            "/p:IntDir=${FOOBAR_SDK_OBJ_DIR}/helpers/"
        COMMAND ${FOOBAR_MSBUILD_COMMAND}
            "${FOOBAR2000_SDK_ROOT_ABSOLUTE}/libPPUI/libPPUI.vcxproj"
            ${FOOBAR_MSBUILD_COMMON_ARGS}
            /p:Configuration=Release
            "/p:OutDir=${FOOBAR_SDK_LIB_DIR}/"
            "/p:IntDir=${FOOBAR_SDK_OBJ_DIR}/ui/"
        BYPRODUCTS
            "${PFC_LIBRARY}"
            "${FOOBAR_SDK_LIBRARY}"
            "${FOOBAR_COMPONENT_CLIENT_LIBRARY}"
            "${FOOBAR_HELPERS_LIBRARY}"
            "${FOOBAR_UI_LIBRARY}"
        VERBATIM
        COMMENT "Building official foobar2000 SDK libraries with MSBuild v143")

    add_library(foo_input_cue_charset MODULE
        src/foobar/charset_analyzer.cpp
        src/foobar/component.cpp
        src/foobar/component_diagnostics.cpp
        src/foobar/cue_album_art.cpp
        src/foobar/cue_document.cpp
        src/foobar/cue_input.cpp
        src/foobar/cue_playlist_loader.cpp
        src/foobar/encoding_preferences.cpp
        src/foobar/encoding_preferences.rc
        src/foobar/flac_duration_filter.cpp
        src/foobar/riff_info_repair.cpp
        src/foobar/wave_info_filter.cpp)
    # Keep the local resource dependency explicit, including on CMake builds
    # without the optional MSVC resource dependency scanner (cmcldeps).
    set_source_files_properties(src/foobar/encoding_preferences.rc PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/foobar/resource.h")
    add_dependencies(foo_input_cue_charset foobar2000_sdk_build)
    set_target_properties(foo_input_cue_charset PROPERTIES
        PREFIX ""
        SUFFIX ".dll"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/component")
    target_compile_features(foo_input_cue_charset PRIVATE cxx_std_20)
    target_compile_definitions(foo_input_cue_charset PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(foo_input_cue_charset PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/permissive-;/EHsc;/utf-8;/d2notypeopt>")
    target_include_directories(foo_input_cue_charset PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_include_directories(foo_input_cue_charset SYSTEM PRIVATE
        "${WTL_INCLUDE_DIR}"
        "${FOOBAR2000_SDK_ROOT_ABSOLUTE}"
        "${FOOBAR2000_SDK_ROOT_ABSOLUTE}/foobar2000")
    target_link_libraries(foo_input_cue_charset PRIVATE
        cue_charset::cue_charset
        "${PFC_LIBRARY}"
        "${FOOBAR_SDK_LIBRARY}"
        "${FOOBAR_COMPONENT_CLIENT_LIBRARY}"
        "${FOOBAR_HELPERS_LIBRARY}"
        "${FOOBAR_UI_LIBRARY}"
        "${FOOBAR2000_SDK_ROOT_ABSOLUTE}/foobar2000/shared/shared-x64.lib"
        winmm)
    target_link_options(foo_input_cue_charset PRIVATE /INCREMENTAL:NO /LTCG)

    set(COMPONENT_DIST_DIR "${CMAKE_CURRENT_BINARY_DIR}/dist")
    set(COMPONENT_STAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}/component-package")
    set(COMPONENT_PACKAGE
        "${COMPONENT_DIST_DIR}/foo_input_cue_charset-${PROJECT_VERSION}-x64.fb2k-component")
    set(COMPONENT_HASH_FILE "${COMPONENT_DIST_DIR}/SHA256SUMS")
    file(MAKE_DIRECTORY "${COMPONENT_STAGE_DIR}")

    add_custom_command(
        OUTPUT "${COMPONENT_PACKAGE}" "${COMPONENT_HASH_FILE}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${COMPONENT_DIST_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:foo_input_cue_charset>"
            "${COMPONENT_STAGE_DIR}/foo_input_cue_charset.dll"
        COMMAND "${CMAKE_COMMAND}"
            "-DPACKAGE_FILE=${COMPONENT_PACKAGE}"
            "-DHASH_FILE=${COMPONENT_HASH_FILE}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/package_component.cmake"
        DEPENDS
            foo_input_cue_charset
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/package_component.cmake"
        WORKING_DIRECTORY "${COMPONENT_STAGE_DIR}"
        VERBATIM
        COMMENT "Packaging foo_input_cue_charset-${PROJECT_VERSION}-x64.fb2k-component")
    add_custom_target(package_component ALL
        DEPENDS "${COMPONENT_PACKAGE}" "${COMPONENT_HASH_FILE}")
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
