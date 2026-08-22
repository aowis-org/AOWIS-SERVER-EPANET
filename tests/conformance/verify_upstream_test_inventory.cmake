cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED AOWIS_SOURCE_ROOT)
    message(FATAL_ERROR "AOWIS_SOURCE_ROOT is required")
endif()
if(NOT DEFINED AOWIS_INVENTORY_FILE)
    message(FATAL_ERROR "AOWIS_INVENTORY_FILE is required")
endif()
if(NOT EXISTS "${AOWIS_INVENTORY_FILE}")
    message(FATAL_ERROR "Upstream test inventory does not exist: ${AOWIS_INVENTORY_FILE}")
endif()
if(NOT DEFINED AOWIS_REGISTERED_SCENARIOS_FILE)
    message(FATAL_ERROR "AOWIS_REGISTERED_SCENARIOS_FILE is required")
endif()
if(NOT EXISTS "${AOWIS_REGISTERED_SCENARIOS_FILE}")
    message(FATAL_ERROR "Registered scenario manifest does not exist: ${AOWIS_REGISTERED_SCENARIOS_FILE}")
endif()

file(STRINGS "${AOWIS_REGISTERED_SCENARIOS_FILE}" AOWIS_REGISTERED_SCENARIOS)
file(STRINGS "${AOWIS_INVENTORY_FILE}" AOWIS_INVENTORY_LINES)
set(AOWIS_INVENTORY_KEYS)
set(AOWIS_WRAPPER_CANDIDATE_COUNT 0)
set(AOWIS_EVIDENCE_LINK_COUNT 0)

foreach(AOWIS_INVENTORY_LINE IN LISTS AOWIS_INVENTORY_LINES)
    if(AOWIS_INVENTORY_LINE STREQUAL "" OR AOWIS_INVENTORY_LINE MATCHES "^#")
        continue()
    endif()

    # CMake uses semicolons as list separators. Preserve semicolons that are
    # part of free-text inventory fields before converting pipe delimiters.
    string(REPLACE ";" "\\;" AOWIS_ESCAPED_INVENTORY_LINE "${AOWIS_INVENTORY_LINE}")
    string(REPLACE "|" ";" AOWIS_INVENTORY_COLUMNS "${AOWIS_ESCAPED_INVENTORY_LINE}")
    list(LENGTH AOWIS_INVENTORY_COLUMNS AOWIS_COLUMN_COUNT)
    if(NOT AOWIS_COLUMN_COUNT EQUAL 7)
        message(FATAL_ERROR "Inventory row must contain exactly seven pipe-separated columns: ${AOWIS_INVENTORY_LINE}")
    endif()

    list(GET AOWIS_INVENTORY_COLUMNS 0 AOWIS_TEST_SOURCE)
    list(GET AOWIS_INVENTORY_COLUMNS 1 AOWIS_TEST_NAME)
    list(GET AOWIS_INVENTORY_COLUMNS 2 AOWIS_TEST_KIND)
    list(GET AOWIS_INVENTORY_COLUMNS 3 AOWIS_TEST_CLASSIFICATION)
    list(GET AOWIS_INVENTORY_COLUMNS 4 AOWIS_COVERAGE_AREA)
    list(GET AOWIS_INVENTORY_COLUMNS 5 AOWIS_EVIDENCE_TEXT)

    if(NOT AOWIS_TEST_KIND MATCHES "^(boost-case|standalone)$")
        message(FATAL_ERROR "Invalid inventory kind for ${AOWIS_TEST_SOURCE}::${AOWIS_TEST_NAME}: ${AOWIS_TEST_KIND}")
    endif()
    if(NOT AOWIS_TEST_CLASSIFICATION MATCHES "^(wrapper-candidate|native-only|not-applicable)$")
        message(FATAL_ERROR "Invalid inventory classification for ${AOWIS_TEST_SOURCE}::${AOWIS_TEST_NAME}: ${AOWIS_TEST_CLASSIFICATION}")
    endif()

    if(AOWIS_TEST_CLASSIFICATION STREQUAL "wrapper-candidate")
        math(EXPR AOWIS_WRAPPER_CANDIDATE_COUNT "${AOWIS_WRAPPER_CANDIDATE_COUNT} + 1")
        if(AOWIS_COVERAGE_AREA STREQUAL "none")
            message(FATAL_ERROR "Wrapper candidate has no coverage area: ${AOWIS_TEST_SOURCE}::${AOWIS_TEST_NAME}")
        endif()
        if(AOWIS_EVIDENCE_TEXT STREQUAL "" OR AOWIS_EVIDENCE_TEXT STREQUAL "none")
            message(FATAL_ERROR "Wrapper candidate has no conformance evidence: ${AOWIS_TEST_SOURCE}::${AOWIS_TEST_NAME}")
        endif()

        string(REPLACE "," ";" AOWIS_EVIDENCE_SCENARIOS "${AOWIS_EVIDENCE_TEXT}")
        foreach(AOWIS_EVIDENCE_SCENARIO IN LISTS AOWIS_EVIDENCE_SCENARIOS)
            string(STRIP "${AOWIS_EVIDENCE_SCENARIO}" AOWIS_EVIDENCE_SCENARIO)
            if(AOWIS_EVIDENCE_SCENARIO STREQUAL "")
                message(FATAL_ERROR "Wrapper candidate contains an empty evidence scenario: ${AOWIS_TEST_SOURCE}::${AOWIS_TEST_NAME}")
            endif()
            if(NOT AOWIS_EVIDENCE_SCENARIO MATCHES "^conformance-")
                message(FATAL_ERROR
                    "Wrapper candidate evidence must name a conformance scenario: "
                    "${AOWIS_TEST_SOURCE}::${AOWIS_TEST_NAME} -> ${AOWIS_EVIDENCE_SCENARIO}"
                )
            endif()
            if(NOT AOWIS_EVIDENCE_SCENARIO IN_LIST AOWIS_REGISTERED_SCENARIOS)
                message(FATAL_ERROR
                    "Wrapper candidate evidence scenario is not registered: "
                    "${AOWIS_TEST_SOURCE}::${AOWIS_TEST_NAME} -> ${AOWIS_EVIDENCE_SCENARIO}"
                )
            endif()
            math(EXPR AOWIS_EVIDENCE_LINK_COUNT "${AOWIS_EVIDENCE_LINK_COUNT} + 1")
        endforeach()
    else()
        if(NOT AOWIS_EVIDENCE_TEXT STREQUAL "none")
            message(FATAL_ERROR
                "Only wrapper-candidate rows may claim AOWIS conformance evidence: "
                "${AOWIS_TEST_SOURCE}::${AOWIS_TEST_NAME}"
            )
        endif()
    endif()

    list(APPEND AOWIS_INVENTORY_KEYS "${AOWIS_TEST_SOURCE}::${AOWIS_TEST_NAME}")
endforeach()

file(
    GLOB_RECURSE
    AOWIS_UPSTREAM_TEST_SOURCES
    RELATIVE "${AOWIS_SOURCE_ROOT}"
    "${AOWIS_SOURCE_ROOT}/external/epanet/tests/*.cpp"
)

set(AOWIS_DISCOVERED_KEYS)
foreach(AOWIS_TEST_SOURCE IN LISTS AOWIS_UPSTREAM_TEST_SOURCES)
    file(
        STRINGS
        "${AOWIS_SOURCE_ROOT}/${AOWIS_TEST_SOURCE}"
        AOWIS_TEST_CASE_LINES
        REGEX "^[ \t]*BOOST_(AUTO|FIXTURE)_TEST_CASE[ \t]*\\("
    )

    foreach(AOWIS_TEST_CASE_LINE IN LISTS AOWIS_TEST_CASE_LINES)
        string(
            REGEX REPLACE
            "^[ \t]*BOOST_(AUTO|FIXTURE)_TEST_CASE[ \t]*\\([ \t]*([A-Za-z0-9_]+).*$"
            "\\2"
            AOWIS_TEST_NAME
            "${AOWIS_TEST_CASE_LINE}"
        )
        list(APPEND AOWIS_DISCOVERED_KEYS "${AOWIS_TEST_SOURCE}::${AOWIS_TEST_NAME}")
    endforeach()
endforeach()

set(AOWIS_REENTRANCY_SOURCE "external/epanet/tests/test_reent.cpp")
if(EXISTS "${AOWIS_SOURCE_ROOT}/${AOWIS_REENTRANCY_SOURCE}")
    file(READ "${AOWIS_SOURCE_ROOT}/${AOWIS_REENTRANCY_SOURCE}" AOWIS_REENTRANCY_CONTENT)
    if(AOWIS_REENTRANCY_CONTENT MATCHES "int[ \t\r\n]+main[ \t\r\n]*\\(")
        list(APPEND AOWIS_DISCOVERED_KEYS "${AOWIS_REENTRANCY_SOURCE}::standalone_reentrancy")
    endif()
endif()

set(AOWIS_MISSING_INVENTORY_KEYS)
foreach(AOWIS_DISCOVERED_KEY IN LISTS AOWIS_DISCOVERED_KEYS)
    if(NOT AOWIS_DISCOVERED_KEY IN_LIST AOWIS_INVENTORY_KEYS)
        list(APPEND AOWIS_MISSING_INVENTORY_KEYS "${AOWIS_DISCOVERED_KEY}")
    endif()
endforeach()

set(AOWIS_STALE_INVENTORY_KEYS)
foreach(AOWIS_INVENTORY_KEY IN LISTS AOWIS_INVENTORY_KEYS)
    if(NOT AOWIS_INVENTORY_KEY IN_LIST AOWIS_DISCOVERED_KEYS)
        list(APPEND AOWIS_STALE_INVENTORY_KEYS "${AOWIS_INVENTORY_KEY}")
    endif()
endforeach()

if(AOWIS_MISSING_INVENTORY_KEYS)
    list(JOIN AOWIS_MISSING_INVENTORY_KEYS "\n  " AOWIS_MISSING_INVENTORY_TEXT)
    message(FATAL_ERROR "Upstream tests missing from inventory:\n  ${AOWIS_MISSING_INVENTORY_TEXT}")
endif()
if(AOWIS_STALE_INVENTORY_KEYS)
    list(JOIN AOWIS_STALE_INVENTORY_KEYS "\n  " AOWIS_STALE_INVENTORY_TEXT)
    message(FATAL_ERROR "Inventory rows not found in the upstream test sources:\n  ${AOWIS_STALE_INVENTORY_TEXT}")
endif()

set(AOWIS_SORTED_INVENTORY_KEYS ${AOWIS_INVENTORY_KEYS})
set(AOWIS_SORTED_DISCOVERED_KEYS ${AOWIS_DISCOVERED_KEYS})
list(SORT AOWIS_SORTED_INVENTORY_KEYS)
list(SORT AOWIS_SORTED_DISCOVERED_KEYS)
if(NOT "${AOWIS_SORTED_INVENTORY_KEYS}" STREQUAL "${AOWIS_SORTED_DISCOVERED_KEYS}")
    message(FATAL_ERROR "Upstream test inventory has the wrong number of occurrences for one or more duplicate test names")
endif()

list(LENGTH AOWIS_DISCOVERED_KEYS AOWIS_DISCOVERED_COUNT)
message(STATUS
    "Verified classifications for ${AOWIS_DISCOVERED_COUNT} active upstream test cases; "
    "${AOWIS_WRAPPER_CANDIDATE_COUNT} wrapper candidates have ${AOWIS_EVIDENCE_LINK_COUNT} registered conformance evidence links"
)
