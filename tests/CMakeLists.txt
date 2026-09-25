set(AOWIS_SERVER_EPANET_CONTRACT_TEST_SCENARIOS
    contract-physical-results-and-leakage
    contract-structured-rule-control
    contract-timer-pump-energy
    contract-reject-pump-power-rule
    contract-steady-state-pump-energy
    contract-quality-model-boundary
    contract-multi-quality-run-envelope
    contract-msx-export-basic-sections
    contract-msx-export-canonical-species-tolerances
    contract-msx-export-species-selection
    contract-msx-export-rejects-unknown-species-selection
    contract-msx-export-rejects-broken-reaction-reference
    contract-msx-integration-end-to-end
    contract-msx-known-answer-fractional-microgram-decay
    contract-msx-formula-equilibrium-known-answer
    contract-msx-fixed-dispersion-peclet-runtime
    contract-msx-parameter-term-overrides-known-answer
    contract-msx-mass-balance-final-summary
    contract-msx-wall-species-pipe-only-results
    contract-msx-configured-inp-roughness-kc
    contract-msx-backend-diagnostics
    contract-msx-cancellation-partial
    contract-msx-output-species-filter-coupled-chemistry
    contract-msx-patterned-mass-source-canonical-units
    contract-msx-remaining-source-modes-canonical-units
    contract-msx-validation-identities
    contract-msx-validation-references-assignments
    contract-msx-validation-numerics-expressions
    contract-msx-validation-public-preflight
    contract-msx-with-standard-quality
    contract-msx-standard-quality-isolated-equivalence
    contract-msx-standard-quality-failure-isolation
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
    conformance-hydraulic-saved-file-reuse
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
    conformance-valve-adjacent-tcv-allowed
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
    conformance-controls-rule-structured-serialization
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
    conformance-reentrancy-independent-runners
    conformance-import-project-globals-net1
    conformance-import-project-globals-canonical-units
    conformance-import-core-topology-net1
    conformance-import-core-topology-canonical-units
    conformance-import-hydraulic-assets-net1
    conformance-import-patterns-curves-pumps-canonical-units
    conformance-import-valves-canonical-units
    conformance-import-controls-net1-equivalence
    conformance-import-structured-rules-canonical-units
    conformance-import-quality-chemical-canonical-units
    conformance-import-quality-water-age
    conformance-import-quality-source-trace
    conformance-import-quality-sources-canonical-units
    conformance-import-quality-mixing-reactions-canonical-units
    conformance-import-quality-net1-equivalence
    conformance-import-geometry-degrees-wgs84
    conformance-import-geometry-epsg3089-wgs84
    conformance-import-geometry-unknown-epsg-null-island
    conformance-import-geometry-meters-null-island
    conformance-import-geometry-feet-null-island
    conformance-import-geometry-arbitrary-null-island
    conformance-import-geometry-missing-layout
    conformance-import-metadata-report-fidelity
    conformance-import-report-setting-threshold-canonical
    conformance-import-report-setting-threshold-diagnostic
    conformance-import-open-error-diagnostic
    conformance-import-quality-roundtrip-official-net1
    conformance-import-quality-roundtrip-official-net2
    conformance-import-quality-roundtrip-official-net3
    conformance-import-quality-roundtrip-upstream-demand-categories
    conformance-import-quality-roundtrip-upstream-tank-overflow
    conformance-import-roundtrip-upstream-network-builder
    conformance-import-roundtrip-upstream-network-builder-small
    conformance-export-native-reopen
    conformance-export-titles-comments-tags
    conformance-export-patterns-curves
    conformance-export-coordinates-vertices
    conformance-export-coordinates-without-backdrop-roundtrip
    conformance-export-report-options
    conformance-export-multiple-quality-rejected
    conformance-quality-input-none
    conformance-quality-input-chemical
    conformance-quality-input-water-age
    conformance-quality-input-source-trace
    conformance-quality-input-tank-mixing-models
    conformance-quality-input-reactions
    conformance-quality-execution-none
    conformance-quality-execution-chemical
    conformance-quality-execution-water-age
    conformance-quality-execution-source-trace
    conformance-quality-execution-independent-timeline
    conformance-quality-execution-cancellation-partial
    conformance-quality-runtime-source-concentration
    conformance-quality-runtime-source-mass-booster
    conformance-quality-runtime-source-flow-paced
    conformance-quality-runtime-source-setpoint
    conformance-quality-runtime-source-pattern
    conformance-quality-runtime-tank-mixing-models
    conformance-quality-runtime-reactions
    conformance-quality-runtime-long-multistep-contract
    conformance-quality-stress-chain-chemical-pattern
    conformance-quality-stress-branch-water-age
    conformance-quality-stress-ring-source-trace
    conformance-quality-stress-grid-reactions-dw
    conformance-quality-stress-dual-source-reactions-hw
    conformance-quality-stress-grid-large-long-cm
    conformance-quality-stress-cancellation-positions
    conformance-multi-quality-sequential-basic
    conformance-multi-quality-isolated-equivalence
    conformance-multi-quality-order-independence
    conformance-multi-quality-repeated-source-trace
    conformance-multi-quality-failure-isolation
    conformance-multi-quality-cancellation-between-runs
    conformance-negative-invalid-identifiers
    conformance-negative-duplicate-node-id
    conformance-negative-duplicate-link-id
    conformance-negative-duplicate-uuid
    conformance-negative-duplicate-curve-id
    conformance-negative-broken-node-reference
    conformance-negative-disabled-node-reference
    conformance-negative-disabled-entity-pruning
    conformance-negative-disabled-control-link-reference
    conformance-negative-disabled-rule-link-reference
    conformance-negative-missing-pattern
    conformance-negative-missing-curve
    conformance-negative-missing-valve-curve
    conformance-negative-invalid-pattern-numeric
    conformance-negative-invalid-curve-numeric
    conformance-negative-invalid-curve-shape
    conformance-negative-missing-typed-curve-references
    conformance-negative-invalid-pipe-numeric
    conformance-negative-invalid-node-numeric
    conformance-negative-invalid-emitter-configuration
    conformance-negative-invalid-quality-configuration
    conformance-negative-invalid-map-numeric
    conformance-negative-missing-map-label-anchor
    conformance-negative-invalid-pump-numeric
    conformance-negative-valve-prv-shared-downstream
    conformance-negative-valve-prv-series
    conformance-negative-valve-psv-shared-upstream
    conformance-negative-valve-psv-series
    conformance-negative-valve-prv-to-psv
    conformance-negative-valve-prv-to-fcv
    conformance-negative-valve-fcv-to-psv
    conformance-negative-invalid-valve-numeric
    conformance-negative-invalid-control-numeric
    conformance-negative-invalid-solver-numeric
    conformance-negative-invalid-report-numeric
    conformance-negative-unsupported-configuration
    conformance-negative-multiple-diagnostics
    conformance-negative-structured-diagnostics
    conformance-stress-chain-small-hw
    conformance-stress-chain-medium-dw
    conformance-stress-branch-medium-hw
    conformance-stress-ring-medium-cm
    conformance-stress-grid-small-hw
    conformance-stress-grid-medium-dw
    conformance-stress-grid-large-hw
    conformance-stress-dual-source-mesh-hw
)

set(AOWIS_SERVER_EPANET_MSX_VENDOR_TEST_SCENARIOS
    proof-msx-vendor-open-init-step-close
    proof-msx-aowis-hydraulic-handoff
    proof-msx-nonpipe-link-parser-aliases
    proof-msx-zero-volume-link-quality-semantics
)

set(AOWIS_SERVER_EPANET_TEST_SCENARIOS
    ${AOWIS_SERVER_EPANET_CONTRACT_TEST_SCENARIOS}
    ${AOWIS_SERVER_EPANET_CONFORMANCE_TEST_SCENARIOS}
    ${AOWIS_SERVER_EPANET_MSX_VENDOR_TEST_SCENARIOS}
)

add_executable(aowis-server-epanet-conformance-tests
    conformance/conformance_test_framework.cpp
    conformance/conformance_test_framework.h
    conformance/conformance_test_main.cpp
    conformance/hydraulic_result_comparator.cpp
    conformance/hydraulic_result_comparator.h
    conformance/generated_network_stress_fixture.cpp
    conformance/generated_network_stress_fixture.h
    conformance/generated_quality_stress_fixture.cpp
    conformance/generated_quality_stress_fixture.h
    conformance/native_epanet_reference_runner.cpp
    conformance/native_epanet_reference_runner.h
    conformance/native_quality_reference_runner.cpp
    conformance/native_quality_reference_runner.h
    conformance/net1_conformance_scenarios.h
    conformance/net1_fixture.cpp
    conformance/net1_fixture.h
    conformance/result_contract_scenarios.h
    conformance/hydraulic_behavior_scenarios.h
    conformance/input_mapping_scenarios.h
    conformance/inp_import_scenarios.h
    conformance/inp_roundtrip_proof.cpp
    conformance/inp_roundtrip_proof.h
    conformance/inp_roundtrip_proof_scenarios.h
    conformance/pump_scenarios.h
    conformance/valve_scenarios.h
    conformance/controls_options_operations_scenarios.h
    conformance/export_fidelity_scenarios.h
    conformance/quality_execution_scenarios.h
    conformance/negative_validation_scenarios.h
    conformance/deterministic_stress_scenarios.h
    conformance/deterministic_quality_stress_scenarios.h
    conformance/multi_quality_execution_scenarios.h
    conformance/msx_vendor_smoke_scenarios.h
    conformance/msx_validation_scenarios.h
    conformance/reentrancy_scenarios.h
    test_net1_conformance.cpp
    test_result_contract.cpp
    test_hydraulic_behavior_conformance.cpp
    test_input_mapping_conformance.cpp
    test_inp_import_conformance.cpp
    test_inp_roundtrip_proof.cpp
    test_pump_conformance.cpp
    test_valve_conformance.cpp
    test_controls_options_operations_conformance.cpp
    test_export_fidelity_conformance.cpp
    test_quality_execution_conformance.cpp
    test_negative_validation_conformance.cpp
    test_deterministic_stress_conformance.cpp
    test_deterministic_quality_stress_conformance.cpp
    test_multi_quality_execution_conformance.cpp
    test_msx_vendor_smoke.cpp
    test_msx_validation_conformance.cpp
    test_reentrancy_conformance.cpp
)

find_package(Threads REQUIRED)

target_link_libraries(aowis-server-epanet-conformance-tests
    PRIVATE
        AOWIS::ServerEpanet
        epanetmsx
        Threads::Threads
)

target_compile_features(aowis-server-epanet-conformance-tests PRIVATE cxx_std_20)

target_compile_definitions(aowis-server-epanet-conformance-tests
    PRIVATE
        AOWIS_EPANET_TEST_NET1_INP="${PROJECT_SOURCE_DIR}/external/epanet/example-networks/Net1.inp"
        AOWIS_EPANET_TEST_NET2_INP="${PROJECT_SOURCE_DIR}/external/epanet/example-networks/Net2.inp"
        AOWIS_EPANET_TEST_NET3_INP="${PROJECT_SOURCE_DIR}/external/epanet/example-networks/Net3.inp"
        AOWIS_EPANET_MSX_EXAMPLE_INP="${PROJECT_SOURCE_DIR}/external/epanet-msx/Examples/example.inp"
        AOWIS_EPANET_MSX_EXAMPLE_MSX="${PROJECT_SOURCE_DIR}/external/epanet-msx/Examples/example.msx"
        AOWIS_EPANET_TEST_UPSTREAM_DEMAND_CATEGORIES_INP="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/inp/upstream_net1_dem_cat.inp"
        AOWIS_EPANET_TEST_UPSTREAM_TANK_OVERFLOW_INP="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/inp/upstream_test_overflow.inp"
        AOWIS_EPANET_TEST_UPSTREAM_NETWORK_BUILDER_INP="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/inp/upstream_net_builder.inp"
        AOWIS_EPANET_TEST_UPSTREAM_NETWORK_BUILDER_SMALL_INP="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/inp/upstream_netbuilder_test2.inp"
        AOWIS_EPANET_TEST_IMPORT_GLOBAL_OPTIONS_US_INP="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/inp/import_global_options_us.inp"
        AOWIS_EPANET_TEST_IMPORT_CORE_TOPOLOGY_US_INP="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/inp/import_core_topology_us.inp"
        AOWIS_EPANET_TEST_IMPORT_PUMPS_US_INP="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/inp/import_patterns_curves_pumps_us.inp"
        AOWIS_EPANET_TEST_IMPORT_VALVES_US_INP="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/inp/import_valves_us.inp"
        AOWIS_EPANET_TEST_IMPORT_CONTROLS_RULES_US_INP="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/inp/import_controls_rules_us.inp"
        AOWIS_EPANET_TEST_IMPORT_QUALITY_CHEMICAL_UG_L_INP="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/inp/import_quality_chemical_ug_l.inp"
        AOWIS_EPANET_TEST_IMPORT_QUALITY_SOURCES_UG_L_INP="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/inp/import_quality_sources_ug_l.inp"
        AOWIS_EPANET_TEST_IMPORT_QUALITY_MIXING_REACTIONS_UG_L_INP="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/inp/import_quality_mixing_reactions_ug_l.inp"
        AOWIS_EPANET_TEST_IMPORT_QUALITY_WALL_REACTION_US_INP="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/inp/import_quality_wall_reaction_us.inp"
        AOWIS_EPANET_TEST_IMPORT_QUALITY_AGE_INP="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/inp/import_quality_age.inp"
        AOWIS_EPANET_TEST_IMPORT_QUALITY_TRACE_INP="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/inp/import_quality_trace.inp"
        AOWIS_EPANET_TEST_IMPORT_GEOMETRY_DEGREES_INP="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/inp/import_geometry_degrees.inp"
        AOWIS_EPANET_TEST_IMPORT_GEOMETRY_EPSG3089_INP="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/inp/import_geometry_epsg3089.inp"
        AOWIS_EPANET_TEST_IMPORT_GEOMETRY_UNKNOWN_EPSG_INP="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/inp/import_geometry_unknown_epsg.inp"
        AOWIS_EPANET_TEST_IMPORT_GEOMETRY_METERS_INP="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/inp/import_geometry_meters.inp"
        AOWIS_EPANET_TEST_IMPORT_GEOMETRY_FEET_INP="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/inp/import_geometry_feet.inp"
        AOWIS_EPANET_TEST_IMPORT_GEOMETRY_ARBITRARY_INP="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/inp/import_geometry_arbitrary.inp"
        AOWIS_EPANET_TEST_IMPORT_GEOMETRY_MISSING_INP="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/inp/import_geometry_missing.inp"
        AOWIS_EPANET_TEST_IMPORT_METADATA_REPORT_US_INP="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/inp/import_metadata_report_us.inp"
        AOWIS_EPANET_TEST_IMPORT_REPORT_SETTING_THRESHOLD_CANONICAL_INP="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/inp/import_report_setting_threshold_canonical.inp"
        AOWIS_EPANET_TEST_IMPORT_REPORT_SETTING_THRESHOLD_US_INP="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/inp/import_report_setting_threshold_us.inp"
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
    if(AOWIS_SERVER_EPANET_TEST_SCENARIO STREQUAL "contract-quality-model-boundary")
        set(AOWIS_SERVER_EPANET_CONTRACT_LABELS "contract;quality")
    elseif(AOWIS_SERVER_EPANET_TEST_SCENARIO STREQUAL "contract-multi-quality-run-envelope")
        set(AOWIS_SERVER_EPANET_CONTRACT_LABELS "contract;hydraulic;quality")
    elseif(AOWIS_SERVER_EPANET_TEST_SCENARIO MATCHES "^contract-msx-export-rejects-")
        set(AOWIS_SERVER_EPANET_CONTRACT_LABELS "contract;quality;negative")
    elseif(AOWIS_SERVER_EPANET_TEST_SCENARIO STREQUAL "contract-msx-validation-public-preflight")
        set(AOWIS_SERVER_EPANET_CONTRACT_LABELS "contract;hydraulic;quality;negative")
    elseif(AOWIS_SERVER_EPANET_TEST_SCENARIO MATCHES "^contract-msx-validation-")
        set(AOWIS_SERVER_EPANET_CONTRACT_LABELS "contract;quality;negative")
    elseif(AOWIS_SERVER_EPANET_TEST_SCENARIO STREQUAL "contract-msx-backend-diagnostics")
        set(AOWIS_SERVER_EPANET_CONTRACT_LABELS "contract;quality;negative")
    elseif(AOWIS_SERVER_EPANET_TEST_SCENARIO STREQUAL "contract-msx-cancellation-partial")
        set(AOWIS_SERVER_EPANET_CONTRACT_LABELS "contract;quality;cancellation")
    elseif(AOWIS_SERVER_EPANET_TEST_SCENARIO STREQUAL "contract-msx-standard-quality-failure-isolation")
        set(AOWIS_SERVER_EPANET_CONTRACT_LABELS "contract;hydraulic;quality;proof;negative")
    elseif(AOWIS_SERVER_EPANET_TEST_SCENARIO STREQUAL "contract-msx-standard-quality-isolated-equivalence"
           OR AOWIS_SERVER_EPANET_TEST_SCENARIO STREQUAL "contract-msx-configured-inp-roughness-kc"
           OR AOWIS_SERVER_EPANET_TEST_SCENARIO STREQUAL "contract-msx-known-answer-fractional-microgram-decay"
           OR AOWIS_SERVER_EPANET_TEST_SCENARIO STREQUAL "contract-msx-wall-species-pipe-only-results")
        set(AOWIS_SERVER_EPANET_CONTRACT_LABELS "contract;hydraulic;quality;proof")
    elseif(AOWIS_SERVER_EPANET_TEST_SCENARIO STREQUAL "contract-msx-integration-end-to-end"
           OR AOWIS_SERVER_EPANET_TEST_SCENARIO STREQUAL "contract-msx-with-standard-quality")
        set(AOWIS_SERVER_EPANET_CONTRACT_LABELS "contract;hydraulic;quality")
    elseif(AOWIS_SERVER_EPANET_TEST_SCENARIO MATCHES "^contract-msx-")
        set(AOWIS_SERVER_EPANET_CONTRACT_LABELS "contract;quality")
    else()
        set(AOWIS_SERVER_EPANET_CONTRACT_LABELS "contract;hydraulic")
    endif()

    set_tests_properties(
        aowis-server-epanet-${AOWIS_SERVER_EPANET_TEST_SCENARIO}
        PROPERTIES
            LABELS "${AOWIS_SERVER_EPANET_CONTRACT_LABELS}"
    )
endforeach()

foreach(AOWIS_SERVER_EPANET_TEST_SCENARIO IN LISTS AOWIS_SERVER_EPANET_MSX_VENDOR_TEST_SCENARIOS)
    if(AOWIS_SERVER_EPANET_TEST_SCENARIO STREQUAL "proof-msx-aowis-hydraulic-handoff"
       OR AOWIS_SERVER_EPANET_TEST_SCENARIO STREQUAL "proof-msx-zero-volume-link-quality-semantics")
        set(AOWIS_SERVER_EPANET_MSX_TEST_LABELS "proof;hydraulic;quality")
    else()
        set(AOWIS_SERVER_EPANET_MSX_TEST_LABELS "proof;quality")
    endif()

    set_tests_properties(
        aowis-server-epanet-${AOWIS_SERVER_EPANET_TEST_SCENARIO}
        PROPERTIES
            LABELS "${AOWIS_SERVER_EPANET_MSX_TEST_LABELS}"
    )
endforeach()

foreach(AOWIS_SERVER_EPANET_TEST_SCENARIO IN LISTS AOWIS_SERVER_EPANET_CONFORMANCE_TEST_SCENARIOS)
    if(AOWIS_SERVER_EPANET_TEST_SCENARIO MATCHES "^conformance-multi-quality-")
        set(AOWIS_SERVER_EPANET_TEST_LABELS "conformance;hydraulic;quality")
    elseif(AOWIS_SERVER_EPANET_TEST_SCENARIO MATCHES "^conformance-quality-|^conformance-import-quality-|^conformance-negative-invalid-quality-")
        set(AOWIS_SERVER_EPANET_TEST_LABELS "conformance;quality")
    else()
        set(AOWIS_SERVER_EPANET_TEST_LABELS "conformance;hydraulic")
    endif()

    if(AOWIS_SERVER_EPANET_TEST_SCENARIO MATCHES "^conformance-negative-|^conformance-multi-quality-failure-isolation$"
       OR AOWIS_SERVER_EPANET_TEST_SCENARIO STREQUAL "conformance-import-open-error-diagnostic"
       OR AOWIS_SERVER_EPANET_TEST_SCENARIO STREQUAL "conformance-import-geometry-unknown-epsg-null-island")
        list(APPEND AOWIS_SERVER_EPANET_TEST_LABELS "negative")
    elseif(AOWIS_SERVER_EPANET_TEST_SCENARIO MATCHES "^conformance-export-")
        list(APPEND AOWIS_SERVER_EPANET_TEST_LABELS "export")
    elseif(AOWIS_SERVER_EPANET_TEST_SCENARIO MATCHES "^conformance-stress-|^conformance-quality-stress-")
        list(APPEND AOWIS_SERVER_EPANET_TEST_LABELS "stress")
    endif()

    if(AOWIS_SERVER_EPANET_TEST_SCENARIO MATCHES "coordinates-vertices|map-label|^conformance-import-geometry-"
       OR AOWIS_SERVER_EPANET_TEST_SCENARIO STREQUAL "conformance-export-coordinates-without-backdrop-roundtrip")
        list(APPEND AOWIS_SERVER_EPANET_TEST_LABELS "coordinate")
    endif()

    if(AOWIS_SERVER_EPANET_TEST_SCENARIO MATCHES "^conformance-import-"
       OR AOWIS_SERVER_EPANET_TEST_SCENARIO STREQUAL "conformance-export-coordinates-without-backdrop-roundtrip")
        list(APPEND AOWIS_SERVER_EPANET_TEST_LABELS "import")
    endif()

    if(AOWIS_SERVER_EPANET_TEST_SCENARIO MATCHES "^conformance-multi-quality-(isolated-equivalence|order-independence)$"
       OR AOWIS_SERVER_EPANET_TEST_SCENARIO STREQUAL "conformance-hydraulic-saved-file-reuse")
        list(APPEND AOWIS_SERVER_EPANET_TEST_LABELS "proof")
    elseif(AOWIS_SERVER_EPANET_TEST_SCENARIO STREQUAL "conformance-reentrancy-independent-runners")
        list(APPEND AOWIS_SERVER_EPANET_TEST_LABELS "quality" "proof")
    elseif(AOWIS_SERVER_EPANET_TEST_SCENARIO STREQUAL "conformance-export-coordinates-without-backdrop-roundtrip")
        list(APPEND AOWIS_SERVER_EPANET_TEST_LABELS "proof")
    elseif(AOWIS_SERVER_EPANET_TEST_SCENARIO STREQUAL "conformance-import-quality-net1-equivalence"
           OR AOWIS_SERVER_EPANET_TEST_SCENARIO MATCHES "^conformance-import-(quality-)?roundtrip-")
        list(APPEND AOWIS_SERVER_EPANET_TEST_LABELS "proof")
    endif()

    set_tests_properties(
        aowis-server-epanet-${AOWIS_SERVER_EPANET_TEST_SCENARIO}
        PROPERTIES
            LABELS "${AOWIS_SERVER_EPANET_TEST_LABELS}"
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
        LABELS "framework;proof"
)

string(REPLACE ";" "\n" AOWIS_SERVER_EPANET_REGISTERED_SCENARIOS "${AOWIS_SERVER_EPANET_TEST_SCENARIOS}")
set(AOWIS_SERVER_EPANET_REGISTERED_SCENARIOS_FILE
    "${CMAKE_CURRENT_BINARY_DIR}/registered_scenarios.txt"
)
file(WRITE
    "${AOWIS_SERVER_EPANET_REGISTERED_SCENARIOS_FILE}"
    "${AOWIS_SERVER_EPANET_REGISTERED_SCENARIOS}\n"
)

add_test(
    NAME aowis-server-epanet-upstream-test-inventory
    COMMAND
        ${CMAKE_COMMAND}
        -DAOWIS_SOURCE_ROOT=${PROJECT_SOURCE_DIR}
        -DAOWIS_INVENTORY_FILE=${CMAKE_CURRENT_SOURCE_DIR}/upstream_test_inventory.txt
        -DAOWIS_REGISTERED_SCENARIOS_FILE=${AOWIS_SERVER_EPANET_REGISTERED_SCENARIOS_FILE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/conformance/verify_upstream_test_inventory.cmake
)
set_tests_properties(
    aowis-server-epanet-upstream-test-inventory
    PROPERTIES
        LABELS "framework;proof;upstream"
)

get_target_property(AOWIS_SERVER_MODEL_REAL_TARGET AOWIS::ServerModel ALIASED_TARGET)
if(NOT AOWIS_SERVER_MODEL_REAL_TARGET)
    set(AOWIS_SERVER_MODEL_REAL_TARGET AOWIS::ServerModel)
endif()

get_target_property(
    AOWIS_SERVER_MODEL_SOURCE_ROOT
    ${AOWIS_SERVER_MODEL_REAL_TARGET}
    SOURCE_DIR
)

if(NOT AOWIS_SERVER_MODEL_SOURCE_ROOT
   OR NOT EXISTS "${AOWIS_SERVER_MODEL_SOURCE_ROOT}/include/aowis/model/hydraulic/network_hydraulic.h")
    get_target_property(
        AOWIS_SERVER_MODEL_INTERFACE_INCLUDE_DIRS
        ${AOWIS_SERVER_MODEL_REAL_TARGET}
        INTERFACE_INCLUDE_DIRECTORIES
    )

    foreach(AOWIS_SERVER_MODEL_INCLUDE_DIR IN LISTS AOWIS_SERVER_MODEL_INTERFACE_INCLUDE_DIRS)
        if(AOWIS_SERVER_MODEL_INCLUDE_DIR MATCHES "^\\$<BUILD_INTERFACE:(.*)>$")
            set(AOWIS_SERVER_MODEL_INCLUDE_DIR "${CMAKE_MATCH_1}")
        elseif(AOWIS_SERVER_MODEL_INCLUDE_DIR MATCHES "^\\$<INSTALL_INTERFACE:")
            continue()
        endif()

        if(EXISTS "${AOWIS_SERVER_MODEL_INCLUDE_DIR}/aowis/model/hydraulic/network_hydraulic.h")
            get_filename_component(
                AOWIS_SERVER_MODEL_SOURCE_ROOT
                "${AOWIS_SERVER_MODEL_INCLUDE_DIR}"
                DIRECTORY
            )
            break()
        endif()
    endforeach()
endif()

if(NOT AOWIS_SERVER_MODEL_SOURCE_ROOT
   OR NOT EXISTS "${AOWIS_SERVER_MODEL_SOURCE_ROOT}/include/aowis/model/hydraulic/network_hydraulic.h")
    message(FATAL_ERROR
        "The model-field conformance audit requires readable AOWIS-SERVER-MODEL hydraulic and water-quality headers."
    )
endif()

add_test(
    NAME aowis-server-epanet-model-field-conformance-audit
    COMMAND
        ${CMAKE_COMMAND}
        -DAOWIS_MODEL_SOURCE_ROOT=${AOWIS_SERVER_MODEL_SOURCE_ROOT}
        -DAOWIS_FIELD_AUDIT_POLICY=${CMAKE_CURRENT_SOURCE_DIR}/model_field_conformance_policy.txt
        -DAOWIS_REGISTERED_SCENARIOS_FILE=${AOWIS_SERVER_EPANET_REGISTERED_SCENARIOS_FILE}
        -DAOWIS_FIELD_AUDIT_REPORT=${CMAKE_CURRENT_BINARY_DIR}/model_field_conformance_audit.tsv
        -P ${CMAKE_CURRENT_SOURCE_DIR}/conformance/verify_model_field_conformance.cmake
)
set_tests_properties(
    aowis-server-epanet-model-field-conformance-audit
    PROPERTIES
        LABELS "framework;proof"
)

if(AOWIS_SERVER_EPANET_ENABLE_UPSTREAM_BOOST_TESTS)
    if(WIN32)
        set(Boost_USE_STATIC_LIBS ON)
    else()
        set(Boost_USE_STATIC_LIBS OFF)
    endif()
    find_package(Boost REQUIRED COMPONENTS unit_test_framework system thread filesystem)

    set(AOWIS_SERVER_EPANET_UPSTREAM_BOOST_BUILD_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/upstream-boost"
    )

    add_test(
        NAME aowis-server-epanet-upstream-boost-suite
        COMMAND
            ${CMAKE_COMMAND}
            -DAOWIS_UPSTREAM_SOURCE_DIR=${PROJECT_SOURCE_DIR}/external/epanet
            -DAOWIS_UPSTREAM_BINARY_DIR=${AOWIS_SERVER_EPANET_UPSTREAM_BOOST_BUILD_DIR}
            -DAOWIS_UPSTREAM_GENERATOR=${CMAKE_GENERATOR}
            -DAOWIS_UPSTREAM_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DAOWIS_UPSTREAM_C_COMPILER=${CMAKE_C_COMPILER}
            -DAOWIS_UPSTREAM_CXX_COMPILER=${CMAKE_CXX_COMPILER}
            -DAOWIS_UPSTREAM_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
            -DAOWIS_UPSTREAM_EXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/conformance/run_upstream_boost_suite.cmake
    )
    set_tests_properties(
        aowis-server-epanet-upstream-boost-suite
        PROPERTIES
            LABELS "upstream;upstream-original"
            RUN_SERIAL TRUE
    )
endif()

if(AOWIS_SERVER_EPANET_ENABLE_COVERAGE)
    set(AOWIS_SERVER_EPANET_COVERAGE_OUTPUT_DIR
        "${CMAKE_BINARY_DIR}/coverage"
    )

    add_custom_target(aowis-server-epanet-coverage
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${AOWIS_SERVER_EPANET_COVERAGE_OUTPUT_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${AOWIS_SERVER_EPANET_COVERAGE_OUTPUT_DIR}"
        COMMAND ${CMAKE_COMMAND}
            -DAOWIS_COVERAGE_BINARY_DIR=${CMAKE_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/conformance/reset_coverage_data.cmake
        COMMAND ${CMAKE_CTEST_COMMAND}
            --test-dir "${CMAKE_BINARY_DIR}"
            -L "^(contract|conformance|framework)$"
            --output-on-failure
        COMMAND ${AOWIS_SERVER_EPANET_GCOVR_EXECUTABLE}
            --root "${PROJECT_SOURCE_DIR}"
            --filter "${PROJECT_SOURCE_DIR}/src/lib"
            --txt "${AOWIS_SERVER_EPANET_COVERAGE_OUTPUT_DIR}/line-coverage.txt"
            "${CMAKE_BINARY_DIR}"
        COMMAND ${AOWIS_SERVER_EPANET_GCOVR_EXECUTABLE}
            --root "${PROJECT_SOURCE_DIR}"
            --filter "${PROJECT_SOURCE_DIR}/src/lib"
            --branches
            --txt "${AOWIS_SERVER_EPANET_COVERAGE_OUTPUT_DIR}/branch-coverage.txt"
            "${CMAKE_BINARY_DIR}"
        COMMAND ${AOWIS_SERVER_EPANET_GCOVR_EXECUTABLE}
            --root "${PROJECT_SOURCE_DIR}"
            --filter "${PROJECT_SOURCE_DIR}/src/lib"
            --html-details "${AOWIS_SERVER_EPANET_COVERAGE_OUTPUT_DIR}/coverage.html"
            "${CMAKE_BINARY_DIR}"
        DEPENDS aowis-server-epanet-conformance-tests
        USES_TERMINAL
        COMMENT "Run AOWIS hydraulic conformance tests and generate line/branch coverage reports"
    )
endif()
