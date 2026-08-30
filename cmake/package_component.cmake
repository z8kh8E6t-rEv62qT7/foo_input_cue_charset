if(NOT DEFINED PACKAGE_FILE OR NOT DEFINED HASH_FILE)
    message(FATAL_ERROR "PACKAGE_FILE and HASH_FILE are required")
endif()
if(NOT EXISTS "foo_input_cue_charset.dll")
    message(FATAL_ERROR "staged foo_input_cue_charset.dll is missing")
endif()

file(ARCHIVE_CREATE
    OUTPUT "${PACKAGE_FILE}"
    PATHS "foo_input_cue_charset.dll"
    FORMAT zip)
file(SHA256 "${PACKAGE_FILE}" package_sha256)
get_filename_component(package_name "${PACKAGE_FILE}" NAME)
file(WRITE "${HASH_FILE}" "${package_sha256}  ${package_name}\n")
