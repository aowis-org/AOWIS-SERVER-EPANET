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
    test_net1_conformance.cpp
    test_result_contract.cpp
    test_upstream_step3_conformance.cpp
    test_upstream_step4_conformance.cpp
    test_upstream_step5_conformance.cpp
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
