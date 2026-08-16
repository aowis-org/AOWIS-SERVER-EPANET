set(AOWIS_SERVER_EPANET_CONTRACT_TEST_SCENARIOS
    contract-physical-results-and-leakage
    contract-structured-rule-control
    contract-timer-pump-energy
    contract-reject-pump-power-rule
    contract-steady-state-pump-energy
)

set(AOWIS_SERVER_EPANET_CONFORMANCE_TEST_SCENARIOS
    conformance-net1
    conformance-upstream-dda-pda
    conformance-upstream-leakage
    conformance-upstream-tank-overflow
    conformance-upstream-pcv
    conformance-upstream-demand-pattern
    conformance-upstream-simple-control
    conformance-upstream-hydraulic-stepping
    conformance-upstream-junction-reservoir-inputs
    conformance-upstream-demand-categories
    conformance-upstream-tank-uniform-area
    conformance-upstream-tank-volume-at-max
    conformance-upstream-tank-volume-curve
    conformance-upstream-pipe-inputs
    conformance-upstream-pipe-darcy-weisbach
    conformance-upstream-pipe-chezy-manning
    conformance-upstream-pump-three-point
    conformance-upstream-pump-multipoint
    conformance-upstream-pump-constant-power
    conformance-upstream-pump-initial-speed
    conformance-upstream-pump-speed-pattern
    conformance-upstream-pump-initial-off
    conformance-upstream-pump-constant-efficiency
    conformance-upstream-pump-efficiency-curve
    conformance-upstream-pump-global-energy
    conformance-upstream-pump-energy-pattern
    conformance-upstream-pump-xhead
    conformance-upstream-pump-xflow
    conformance-upstream-valve-prv
    conformance-upstream-valve-psv
    conformance-upstream-valve-pbv
    conformance-upstream-valve-fcv
    conformance-upstream-valve-tcv
    conformance-upstream-valve-gpv
    conformance-controls-simple-low-level
    conformance-controls-simple-high-level
    conformance-controls-simple-timer
    conformance-controls-simple-time-of-day
    conformance-controls-action-open
    conformance-controls-action-close
    conformance-controls-action-setting
    conformance-controls-simple-disabled
    conformance-controls-rule-logical-if
    conformance-controls-rule-logical-and
    conformance-controls-rule-logical-or
    conformance-controls-rule-then
    conformance-controls-rule-else
    conformance-controls-rule-setting-action
    conformance-controls-rule-active-action
    conformance-controls-rule-source-text
    conformance-controls-rule-priority
    conformance-controls-rule-disabled
    conformance-controls-premise-node-demand
    conformance-controls-premise-node-head
    conformance-controls-premise-node-grade
    conformance-controls-premise-node-level
    conformance-controls-premise-node-pressure
    conformance-controls-premise-node-fill-time
    conformance-controls-premise-node-drain-time
    conformance-controls-premise-link-flow
    conformance-controls-premise-link-status
    conformance-controls-premise-link-setting
    conformance-controls-premise-system-demand
    conformance-controls-premise-system-time
    conformance-controls-premise-system-clock-time
    conformance-controls-operator-equal
    conformance-controls-operator-not-equal
    conformance-controls-operator-less-or-equal
    conformance-controls-operator-greater-or-equal
    conformance-controls-operator-less
    conformance-controls-operator-greater
    conformance-controls-operator-is
    conformance-controls-operator-is-not
    conformance-controls-operator-below
    conformance-controls-operator-above
    conformance-controls-next-hydraulic-event
    conformance-controls-control-event-identification
    conformance-options-time-duration
    conformance-options-time-hydraulic-step
    conformance-options-time-quality-step
    conformance-options-time-pattern-step
    conformance-options-time-pattern-start
    conformance-options-time-report-step
    conformance-options-time-report-start
    conformance-options-time-rule-step
    conformance-options-time-start-clock
    conformance-options-report-statistic-series
    conformance-options-report-statistic-average
    conformance-options-report-statistic-minimum
    conformance-options-report-statistic-maximum
    conformance-options-report-statistic-range
    conformance-options-hydraulic-headloss-hazen-williams
    conformance-options-hydraulic-demand-model-dda
    conformance-options-hydraulic-demand-model-pda
    conformance-options-hydraulic-minimum-pressure
    conformance-options-hydraulic-required-pressure
    conformance-options-hydraulic-pressure-exponent
    conformance-options-hydraulic-maximum-trials
    conformance-options-hydraulic-accuracy
    conformance-options-hydraulic-unbalanced-stop
    conformance-options-hydraulic-unbalanced-continue
    conformance-options-hydraulic-check-frequency
    conformance-options-hydraulic-maximum-check
    conformance-options-hydraulic-damping-limit
    conformance-options-hydraulic-maximum-head-error
    conformance-options-hydraulic-maximum-flow-change
    conformance-options-hydraulic-demand-multiplier
    conformance-options-hydraulic-default-demand-pattern-none
    conformance-options-hydraulic-default-demand-pattern
    conformance-options-hydraulic-emitter-exponent
    conformance-options-hydraulic-emitter-backflow-disabled
    conformance-options-hydraulic-emitter-backflow-enabled
    conformance-options-hydraulic-specific-gravity
    conformance-options-hydraulic-relative-viscosity
    conformance-operational-statistics
    conformance-operational-flow-balance
    conformance-operational-warning-diagnostics
    conformance-operational-error-diagnostics
    conformance-operational-cancellation-before-results
    conformance-operational-cancellation-partial-results
    conformance-export-native-reopen
    conformance-export-titles-comments-tags
    conformance-export-patterns-curves
    conformance-export-coordinates-vertices
    conformance-export-report-options
)

set(AOWIS_SERVER_EPANET_TEST_SCENARIOS
    ${AOWIS_SERVER_EPANET_CONTRACT_TEST_SCENARIOS}
    ${AOWIS_SERVER_EPANET_CONFORMANCE_TEST_SCENARIOS}
)

add_executable(aowis-server-epanet-conformance-tests
    conformance/conformance_test_framework.cpp
    conformance/conformance_test_framework.h
    conformance/conformance_test_main.cpp
    conformance/hydraulic_result_comparator.cpp
    conformance/hydraulic_result_comparator.h
    conformance/native_epanet_reference_runner.cpp
    conformance/native_epanet_reference_runner.h
    conformance/net1_conformance_scenarios.h
    conformance/net1_fixture.cpp
    conformance/net1_fixture.h
    conformance/result_contract_scenarios.h
    conformance/upstream_step3_scenarios.h
    conformance/upstream_step4_scenarios.h
    conformance/upstream_step5_scenarios.h
    conformance/upstream_step6_scenarios.h
    conformance/upstream_step7_scenarios.h
    conformance/upstream_step8a_scenarios.h
    test_net1_conformance.cpp
    test_result_contract.cpp
    test_upstream_step3_conformance.cpp
    test_upstream_step4_conformance.cpp
    test_upstream_step5_conformance.cpp
    test_upstream_step6_conformance.cpp
    test_upstream_step7_conformance.cpp
    test_upstream_step8a_conformance.cpp
)

target_link_libraries(aowis-server-epanet-conformance-tests
    PRIVATE
        AOWIS::ServerEpanet
)

target_compile_features(aowis-server-epanet-conformance-tests PRIVATE cxx_std_20)

target_compile_definitions(aowis-server-epanet-conformance-tests
    PRIVATE
        AOWIS_EPANET_TEST_NET1_INP="${PROJECT_SOURCE_DIR}/external/epanet/example-networks/Net1.inp"
)

foreach(AOWIS_SERVER_EPANET_TEST_SCENARIO IN LISTS AOWIS_SERVER_EPANET_TEST_SCENARIOS)
    add_test(
        NAME aowis-server-epanet-${AOWIS_SERVER_EPANET_TEST_SCENARIO}
        COMMAND
            $<TARGET_FILE:aowis-server-epanet-conformance-tests>
            --scenario
            ${AOWIS_SERVER_EPANET_TEST_SCENARIO}
    )
endforeach()

foreach(AOWIS_SERVER_EPANET_TEST_SCENARIO IN LISTS AOWIS_SERVER_EPANET_CONTRACT_TEST_SCENARIOS)
    set_tests_properties(
        aowis-server-epanet-${AOWIS_SERVER_EPANET_TEST_SCENARIO}
        PROPERTIES
            LABELS "contract;hydraulic"
    )
endforeach()

foreach(AOWIS_SERVER_EPANET_TEST_SCENARIO IN LISTS AOWIS_SERVER_EPANET_CONFORMANCE_TEST_SCENARIOS)
    set_tests_properties(
        aowis-server-epanet-${AOWIS_SERVER_EPANET_TEST_SCENARIO}
        PROPERTIES
            LABELS "conformance;hydraulic;upstream"
    )
endforeach()

add_test(
    NAME aowis-server-epanet-test-scenario-manifest
    COMMAND
        $<TARGET_FILE:aowis-server-epanet-conformance-tests>
        --verify-scenarios
        ${AOWIS_SERVER_EPANET_TEST_SCENARIOS}
)
set_tests_properties(
    aowis-server-epanet-test-scenario-manifest
    PROPERTIES
        LABELS "framework"
)

add_test(
    NAME aowis-server-epanet-upstream-test-inventory
    COMMAND
        ${CMAKE_COMMAND}
        -DAOWIS_SOURCE_ROOT=${PROJECT_SOURCE_DIR}
        -DAOWIS_INVENTORY_FILE=${CMAKE_CURRENT_SOURCE_DIR}/upstream_test_inventory.txt
        -P ${CMAKE_CURRENT_SOURCE_DIR}/conformance/verify_upstream_test_inventory.cmake
)
set_tests_properties(
    aowis-server-epanet-upstream-test-inventory
    PROPERTIES
        LABELS "framework;upstream"
)
