if(NOT EXISTS "${PACKAGE_FILE}")
    message(FATAL_ERROR "component package does not exist: ${PACKAGE_FILE}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar tf "${PACKAGE_FILE}"
    RESULT_VARIABLE list_result
    OUTPUT_VARIABLE package_entries
    ERROR_VARIABLE list_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT list_result EQUAL 0)
    message(FATAL_ERROR "cannot list component package: ${list_error}")
endif()
if(NOT package_entries STREQUAL "foo_input_cue_charset.dll")
    message(FATAL_ERROR "unexpected component package entries: ${package_entries}")
endif()
