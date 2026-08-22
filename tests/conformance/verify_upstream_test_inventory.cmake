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

file(STRINGS "${AOWIS_INVENTORY_FILE}" AOWIS_INVENTORY_LINES)
set(AOWIS_INVENTORY_KEYS)

foreach(AOWIS_INVENTORY_LINE IN LISTS AOWIS_INVENTORY_LINES)
    if(AOWIS_INVENTORY_LINE STREQUAL "" OR AOWIS_INVENTORY_LINE MATCHES "^#")
        continue()
    endif()

    # CMake uses semicolons as list separators. Preserve semicolons that are
    # part of free-text inventory fields before converting pipe delimiters.
    string(REPLACE ";" "\\;" AOWIS_ESCAPED_INVENTORY_LINE "${AOWIS_INVENTORY_LINE}")
    string(REPLACE "|" ";" AOWIS_INVENTORY_COLUMNS "${AOWIS_ESCAPED_INVENTORY_LINE}")
    list(LENGTH AOWIS_INVENTORY_COLUMNS AOWIS_COLUMN_COUNT)
    if(NOT AOWIS_COLUMN_COUNT EQUAL 6)
        message(FATAL_ERROR "Inventory row must contain exactly six pipe-separated columns: ${AOWIS_INVENTORY_LINE}")
    endif()

    list(GET AOWIS_INVENTORY_COLUMNS 0 AOWIS_TEST_SOURCE)
    list(GET AOWIS_INVENTORY_COLUMNS 1 AOWIS_TEST_NAME)
    list(GET AOWIS_INVENTORY_COLUMNS 2 AOWIS_TEST_KIND)
    list(GET AOWIS_INVENTORY_COLUMNS 3 AOWIS_TEST_CLASSIFICATION)

    if(NOT AOWIS_TEST_KIND MATCHES "^(boost-case|standalone)$")
        message(FATAL_ERROR "Invalid inventory kind for ${AOWIS_TEST_SOURCE}::${AOWIS_TEST_NAME}: ${AOWIS_TEST_KIND}")
    endif()
    if(NOT AOWIS_TEST_CLASSIFICATION MATCHES "^(wrapper-candidate|native-only|not-applicable)$")
        message(FATAL_ERROR "Invalid inventory classification for ${AOWIS_TEST_SOURCE}::${AOWIS_TEST_NAME}: ${AOWIS_TEST_CLASSIFICATION}")
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
message(STATUS "Verified classifications for ${AOWIS_DISCOVERED_COUNT} active upstream test cases")
