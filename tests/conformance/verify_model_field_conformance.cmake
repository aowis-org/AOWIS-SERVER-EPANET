cmake_minimum_required(VERSION 3.21)

foreach(AOWIS_REQUIRED_VARIABLE IN ITEMS
    AOWIS_MODEL_SOURCE_ROOT
    AOWIS_FIELD_AUDIT_POLICY
    AOWIS_REGISTERED_SCENARIOS_FILE
    AOWIS_FIELD_AUDIT_REPORT
)
    if(NOT DEFINED ${AOWIS_REQUIRED_VARIABLE} OR "${${AOWIS_REQUIRED_VARIABLE}}" STREQUAL "")
        message(FATAL_ERROR "Missing required variable: ${AOWIS_REQUIRED_VARIABLE}")
    endif()
endforeach()

if(NOT EXISTS "${AOWIS_FIELD_AUDIT_POLICY}")
    message(FATAL_ERROR "Model-field audit policy not found: ${AOWIS_FIELD_AUDIT_POLICY}")
endif()
if(NOT EXISTS "${AOWIS_REGISTERED_SCENARIOS_FILE}")
    message(FATAL_ERROR "Registered scenario manifest not found: ${AOWIS_REGISTERED_SCENARIOS_FILE}")
endif()

string(ASCII 9 AOWIS_TAB)
string(ASCII 10 AOWIS_NEWLINE)

file(STRINGS "${AOWIS_REGISTERED_SCENARIOS_FILE}" AOWIS_REGISTERED_SCENARIOS)
list(REMOVE_ITEM AOWIS_REGISTERED_SCENARIOS "")
list(REMOVE_DUPLICATES AOWIS_REGISTERED_SCENARIOS)

file(STRINGS "${AOWIS_FIELD_AUDIT_POLICY}" AOWIS_POLICY_LINES)
set(AOWIS_AUDITED_HEADERS "")
set(AOWIS_POLICY_FIELDS "")
set(AOWIS_POLICY_STRUCTS "")
set(AOWIS_REPORT_ROWS "")
set(AOWIS_COMPLETE_FIELD_COUNT 0)
set(AOWIS_EXCLUDED_NON_EPANET_FIELD_COUNT 0)
set(AOWIS_EXCLUDED_RUNTIME_FIELD_COUNT 0)

foreach(AOWIS_POLICY_LINE IN LISTS AOWIS_POLICY_LINES)
    string(STRIP "${AOWIS_POLICY_LINE}" AOWIS_POLICY_LINE)
    if(AOWIS_POLICY_LINE STREQUAL "" OR AOWIS_POLICY_LINE MATCHES "^#")
        continue()
    endif()

    if(AOWIS_POLICY_LINE MATCHES "^@headers\\|")
        string(REGEX REPLACE "^@headers\\|" "" AOWIS_HEADER_CSV "${AOWIS_POLICY_LINE}")
        string(REPLACE "," ";" AOWIS_AUDITED_HEADERS "${AOWIS_HEADER_CSV}")
        continue()
    endif()

    string(REPLACE "|" ";" AOWIS_POLICY_COLUMNS "${AOWIS_POLICY_LINE}")
    list(LENGTH AOWIS_POLICY_COLUMNS AOWIS_POLICY_COLUMN_COUNT)
    if(NOT AOWIS_POLICY_COLUMN_COUNT EQUAL 6)
        message(FATAL_ERROR "Invalid model-field audit policy row: ${AOWIS_POLICY_LINE}")
    endif()

    list(GET AOWIS_POLICY_COLUMNS 0 AOWIS_HEADER)
    list(GET AOWIS_POLICY_COLUMNS 1 AOWIS_STRUCT)
    list(GET AOWIS_POLICY_COLUMNS 2 AOWIS_FIELDS_CSV)
    list(GET AOWIS_POLICY_COLUMNS 3 AOWIS_STATE)
    list(GET AOWIS_POLICY_COLUMNS 4 AOWIS_EVIDENCE_CSV)
    list(GET AOWIS_POLICY_COLUMNS 5 AOWIS_NOTE)

    if(NOT AOWIS_STATE MATCHES "^(complete|excluded-non-epanet|excluded-runtime-metadata)$")
        message(FATAL_ERROR "Unsupported audit state '${AOWIS_STATE}' in row: ${AOWIS_POLICY_LINE}")
    endif()

    if(AOWIS_STATE STREQUAL "complete")
        if(AOWIS_EVIDENCE_CSV STREQUAL "")
            message(FATAL_ERROR "Complete field audit row has no evidence: ${AOWIS_POLICY_LINE}")
        endif()
        string(REPLACE "," ";" AOWIS_EVIDENCE_SCENARIOS "${AOWIS_EVIDENCE_CSV}")
        foreach(AOWIS_EVIDENCE_SCENARIO IN LISTS AOWIS_EVIDENCE_SCENARIOS)
            if(NOT AOWIS_EVIDENCE_SCENARIO IN_LIST AOWIS_REGISTERED_SCENARIOS)
                message(FATAL_ERROR
                    "Model-field audit references unknown scenario '${AOWIS_EVIDENCE_SCENARIO}' "
                    "for ${AOWIS_STRUCT}: ${AOWIS_FIELDS_CSV}"
                )
            endif()
        endforeach()
    else()
        if(NOT AOWIS_EVIDENCE_CSV STREQUAL "")
            message(FATAL_ERROR "Excluded field audit row must not claim scenario evidence: ${AOWIS_POLICY_LINE}")
        endif()
        if(AOWIS_NOTE STREQUAL "")
            message(FATAL_ERROR "Excluded field audit row must explain the exclusion: ${AOWIS_POLICY_LINE}")
        endif()
    endif()

    set(AOWIS_STRUCT_KEY "${AOWIS_HEADER}|${AOWIS_STRUCT}")
    list(APPEND AOWIS_POLICY_STRUCTS "${AOWIS_STRUCT_KEY}")

    string(REPLACE "," ";" AOWIS_FIELDS "${AOWIS_FIELDS_CSV}")
    foreach(AOWIS_FIELD IN LISTS AOWIS_FIELDS)
        if(AOWIS_FIELD STREQUAL "")
            message(FATAL_ERROR "Empty field name in audit row: ${AOWIS_POLICY_LINE}")
        endif()

        set(AOWIS_FIELD_KEY "${AOWIS_HEADER}|${AOWIS_STRUCT}|${AOWIS_FIELD}")
        if(AOWIS_FIELD_KEY IN_LIST AOWIS_POLICY_FIELDS)
            message(FATAL_ERROR "Duplicate model-field audit entry: ${AOWIS_FIELD_KEY}")
        endif()
        list(APPEND AOWIS_POLICY_FIELDS "${AOWIS_FIELD_KEY}")

        if(AOWIS_STATE STREQUAL "complete")
            math(EXPR AOWIS_COMPLETE_FIELD_COUNT "${AOWIS_COMPLETE_FIELD_COUNT} + 1")
        elseif(AOWIS_STATE STREQUAL "excluded-non-epanet")
            math(EXPR AOWIS_EXCLUDED_NON_EPANET_FIELD_COUNT "${AOWIS_EXCLUDED_NON_EPANET_FIELD_COUNT} + 1")
        elseif(AOWIS_STATE STREQUAL "excluded-runtime-metadata")
            math(EXPR AOWIS_EXCLUDED_RUNTIME_FIELD_COUNT "${AOWIS_EXCLUDED_RUNTIME_FIELD_COUNT} + 1")
        endif()

        string(APPEND AOWIS_REPORT_ROWS
            "${AOWIS_HEADER}${AOWIS_TAB}${AOWIS_STRUCT}${AOWIS_TAB}${AOWIS_FIELD}${AOWIS_TAB}${AOWIS_STATE}${AOWIS_TAB}${AOWIS_EVIDENCE_CSV}${AOWIS_TAB}${AOWIS_NOTE}${AOWIS_NEWLINE}"
        )
    endforeach()
endforeach()

if(AOWIS_AUDITED_HEADERS STREQUAL "")
    message(FATAL_ERROR "Model-field audit policy does not declare @headers")
endif()
list(REMOVE_DUPLICATES AOWIS_POLICY_STRUCTS)

set(AOWIS_MODEL_FIELDS "")
set(AOWIS_MODEL_STRUCTS "")
foreach(AOWIS_HEADER IN LISTS AOWIS_AUDITED_HEADERS)
    set(AOWIS_HEADER_PATH "${AOWIS_MODEL_SOURCE_ROOT}/include/aowis/model/hydraulic/${AOWIS_HEADER}")
    if(NOT EXISTS "${AOWIS_HEADER_PATH}")
        message(FATAL_ERROR "Audited Model header not found: ${AOWIS_HEADER_PATH}")
    endif()

    file(STRINGS "${AOWIS_HEADER_PATH}" AOWIS_HEADER_LINES)
    set(AOWIS_CURRENT_STRUCT "")
    set(AOWIS_WAITING_FOR_STRUCT_BODY FALSE)
    set(AOWIS_IN_STRUCT FALSE)
    set(AOWIS_IN_BLOCK_COMMENT FALSE)

    foreach(AOWIS_SOURCE_LINE IN LISTS AOWIS_HEADER_LINES)
        string(STRIP "${AOWIS_SOURCE_LINE}" AOWIS_SOURCE_LINE)

        if(AOWIS_IN_BLOCK_COMMENT)
            if(AOWIS_SOURCE_LINE MATCHES "\\*/")
                set(AOWIS_IN_BLOCK_COMMENT FALSE)
            endif()
            continue()
        endif()
        if(AOWIS_SOURCE_LINE MATCHES "^/\\*")
            if(NOT AOWIS_SOURCE_LINE MATCHES "\\*/")
                set(AOWIS_IN_BLOCK_COMMENT TRUE)
            endif()
            continue()
        endif()

        if(NOT AOWIS_IN_STRUCT AND NOT AOWIS_WAITING_FOR_STRUCT_BODY
           AND AOWIS_SOURCE_LINE MATCHES "^struct[ ]+([A-Za-z_][A-Za-z0-9_]*)[ ]*$")
            set(AOWIS_CURRENT_STRUCT "${CMAKE_MATCH_1}")
            set(AOWIS_WAITING_FOR_STRUCT_BODY TRUE)
            continue()
        endif()

        if(AOWIS_WAITING_FOR_STRUCT_BODY)
            if(AOWIS_SOURCE_LINE STREQUAL "{")
                set(AOWIS_WAITING_FOR_STRUCT_BODY FALSE)
                set(AOWIS_IN_STRUCT TRUE)
                set(AOWIS_STRUCT_KEY "${AOWIS_HEADER}|${AOWIS_CURRENT_STRUCT}")
                list(APPEND AOWIS_MODEL_STRUCTS "${AOWIS_STRUCT_KEY}")
                continue()
            endif()
            message(FATAL_ERROR "Unexpected Model struct declaration format for ${AOWIS_CURRENT_STRUCT} in ${AOWIS_HEADER}")
        endif()

        if(AOWIS_IN_STRUCT)
            if(AOWIS_SOURCE_LINE MATCHES "^};")
                set(AOWIS_IN_STRUCT FALSE)
                set(AOWIS_CURRENT_STRUCT "")
                continue()
            endif()
            if(AOWIS_SOURCE_LINE STREQUAL "" OR AOWIS_SOURCE_LINE MATCHES "^//")
                continue()
            endif()

            string(REGEX REPLACE "//.*$" "" AOWIS_MEMBER_LINE "${AOWIS_SOURCE_LINE}")
            string(STRIP "${AOWIS_MEMBER_LINE}" AOWIS_MEMBER_LINE)
            if(NOT AOWIS_MEMBER_LINE MATCHES ";$")
                continue()
            endif()

            string(REGEX REPLACE "[ ]*=[^;]*;$" ";" AOWIS_MEMBER_LINE "${AOWIS_MEMBER_LINE}")
            if(NOT AOWIS_MEMBER_LINE MATCHES "([A-Za-z_][A-Za-z0-9_]*)[ ]*;$")
                message(FATAL_ERROR
                    "Could not parse Model field in ${AOWIS_HEADER}::${AOWIS_CURRENT_STRUCT}: ${AOWIS_SOURCE_LINE}"
                )
            endif()

            set(AOWIS_FIELD "${CMAKE_MATCH_1}")
            set(AOWIS_FIELD_KEY "${AOWIS_HEADER}|${AOWIS_CURRENT_STRUCT}|${AOWIS_FIELD}")
            list(APPEND AOWIS_MODEL_FIELDS "${AOWIS_FIELD_KEY}")
        endif()
    endforeach()
endforeach()

list(REMOVE_DUPLICATES AOWIS_MODEL_STRUCTS)

foreach(AOWIS_MODEL_STRUCT IN LISTS AOWIS_MODEL_STRUCTS)
    if(NOT AOWIS_MODEL_STRUCT IN_LIST AOWIS_POLICY_STRUCTS)
        message(FATAL_ERROR "Model struct is missing from model-field audit policy: ${AOWIS_MODEL_STRUCT}")
    endif()
endforeach()
foreach(AOWIS_POLICY_STRUCT IN LISTS AOWIS_POLICY_STRUCTS)
    if(NOT AOWIS_POLICY_STRUCT IN_LIST AOWIS_MODEL_STRUCTS)
        message(FATAL_ERROR "Model-field audit policy references missing Model struct: ${AOWIS_POLICY_STRUCT}")
    endif()
endforeach()

foreach(AOWIS_MODEL_FIELD IN LISTS AOWIS_MODEL_FIELDS)
    if(NOT AOWIS_MODEL_FIELD IN_LIST AOWIS_POLICY_FIELDS)
        message(FATAL_ERROR "Model field is missing from model-field audit policy: ${AOWIS_MODEL_FIELD}")
    endif()
endforeach()
foreach(AOWIS_POLICY_FIELD IN LISTS AOWIS_POLICY_FIELDS)
    if(NOT AOWIS_POLICY_FIELD IN_LIST AOWIS_MODEL_FIELDS)
        message(FATAL_ERROR "Model-field audit policy references missing Model field: ${AOWIS_POLICY_FIELD}")
    endif()
endforeach()

list(LENGTH AOWIS_MODEL_FIELDS AOWIS_MODEL_FIELD_COUNT)
list(LENGTH AOWIS_MODEL_STRUCTS AOWIS_MODEL_STRUCT_COUNT)

get_filename_component(AOWIS_FIELD_AUDIT_REPORT_DIR "${AOWIS_FIELD_AUDIT_REPORT}" DIRECTORY)
file(MAKE_DIRECTORY "${AOWIS_FIELD_AUDIT_REPORT_DIR}")
file(WRITE "${AOWIS_FIELD_AUDIT_REPORT}"
    "header${AOWIS_TAB}struct${AOWIS_TAB}field${AOWIS_TAB}state${AOWIS_TAB}evidence_scenarios${AOWIS_TAB}note${AOWIS_NEWLINE}"
    "${AOWIS_REPORT_ROWS}"
)

message(STATUS
    "Model-field audit passed: ${AOWIS_MODEL_FIELD_COUNT} fields across ${AOWIS_MODEL_STRUCT_COUNT} structs; "
    "${AOWIS_COMPLETE_FIELD_COUNT} complete, ${AOWIS_EXCLUDED_NON_EPANET_FIELD_COUNT} non-EPANET, "
    "${AOWIS_EXCLUDED_RUNTIME_FIELD_COUNT} runtime-metadata."
)
message(STATUS "Model-field audit report: ${AOWIS_FIELD_AUDIT_REPORT}")
