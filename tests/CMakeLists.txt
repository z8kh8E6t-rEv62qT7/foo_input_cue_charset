add_library(fake_icu SHARED fake_icu.cpp)
set_target_properties(fake_icu PROPERTIES PREFIX "")

add_executable(cue_charset_tests
    riff_info_tests.cpp
    test_main.cpp)
target_link_libraries(cue_charset_tests PRIVATE cue_charset::cue_charset)
target_compile_features(cue_charset_tests PRIVATE cxx_std_20)
target_include_directories(cue_charset_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
add_dependencies(cue_charset_tests fake_icu)

if(MSVC)
    target_compile_options(fake_icu PRIVATE /W4 /WX /permissive- /EHsc)
    target_compile_options(cue_charset_tests PRIVATE /W4 /WX /permissive- /EHsc /utf-8)
endif()

set(FAKE_ICU_ROOT "$<TARGET_FILE_DIR:cue_charset_tests>/fake-icu")
add_custom_command(TARGET cue_charset_tests POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${FAKE_ICU_ROOT}/bin"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:fake_icu>" "${FAKE_ICU_ROOT}/bin/libc++.dll"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:fake_icu>" "${FAKE_ICU_ROOT}/bin/libicudt77.dll"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:fake_icu>" "${FAKE_ICU_ROOT}/bin/libicuuc77.dll"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:fake_icu>" "${FAKE_ICU_ROOT}/bin/libicuin77.dll"
    VERBATIM
)

add_test(NAME core
    COMMAND cue_charset_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
        "${ICU_ROOT_ABSOLUTE}"
        "${FAKE_ICU_ROOT}"
        "${CMAKE_CURRENT_BINARY_DIR}/tmp")

add_test(NAME cli_valid
    COMMAND cue-charset "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/gb18030.cue")
set_tests_properties(cli_valid PROPERTIES
    PASS_REGULAR_EXPRESSION "encoding=GB18030.*conversion=success.*replacements=0")

add_test(NAME cli_no_arguments COMMAND cue-charset)
set_tests_properties(cli_no_arguments PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli_too_many_arguments
    COMMAND cue-charset
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/gb18030.cue"
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/gb18030.cue")
set_tests_properties(cli_too_many_arguments PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli_wrong_extension
    COMMAND cue-charset "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/expected-utf8.txt")
set_tests_properties(cli_wrong_extension PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli_missing_file
    COMMAND cue-charset "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/missing.cue")
set_tests_properties(cli_missing_file PROPERTIES WILL_FAIL TRUE)

add_test(NAME fixture_hashes
    COMMAND "${CMAKE_COMMAND}" -E sha256sum
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/gb18030.cue"
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/gb18030-corrupt.cue"
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/expected-utf8.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/gb18030-big5-misdetection.hex")
set_tests_properties(fixture_hashes PROPERTIES PASS_REGULAR_EXPRESSION
    "4c30c28c54de7a6da97ccf0c85b60e5f796682fc045e1265dc43b497dbe22a41.*a0120b7076afd65a82dd95cfb3c9104b3c4403215c1c2fe25c0b5dbd5fa479d4.*f7441dbcc49f9728ae5e612ac0593512a6d630b7bf03d90993c6be7467cbe173.*ecc1bbffd7faab5b50c0359f59df9093b852bb28c844e5958c4647c4c044d51b")

if(BUILD_FOOBAR2000_COMPONENT)
    add_test(NAME component_package
        COMMAND "${CMAKE_COMMAND}"
            "-DPACKAGE_FILE=${COMPONENT_PACKAGE}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_package.cmake")
endif()

add_test(NAME architecture_guard
    COMMAND "${CMAKE_COMMAND}"
        "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_architecture.cmake")
