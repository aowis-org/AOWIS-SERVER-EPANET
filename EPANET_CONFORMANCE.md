# EPANET Adapter Conformance

This document defines the evidence used to verify that the AOWIS model-driven adapter reproduces EPANET 2.3 simulation behavior.

## Conformance target

The conformance target is the public `EpanetRunner` contract:

- translate supported `NetworkHydraulic` inputs into a native EPANET project;
- execute one complete hydraulic analysis;
- execute zero or more chemical, water-age, or source-trace analyses against the saved hydraulic solution;
- return all modeled hydraulic and quality results, statuses, diagnostics, and partial-result states;
- preserve supported INP data during export.

Native project CRUD, arbitrary INP import, direct project-handle access, and public binary Output API access are outside this target because they are not operations exposed by `EpanetRunner`.

## Evidence model

No single test style is accepted as sufficient. The suite combines:

| Evidence | Purpose |
|---|---|
| Contract tests | Verify stable public result, diagnostic, orchestration, and cancellation semantics. |
| Native differential tests | Run equivalent AOWIS and raw Toolkit projects and compare every applicable timestep and result field. |
| Native readback tests | Verify translated options and properties through the Toolkit. |
| INP reopen tests | Export, reopen with native EPANET, and verify persisted metadata and configuration. |
| Negative tests | Verify deterministic rejection of invalid values, broken references, duplicates, and unsupported configurations. |
| Generated stress tests | Compare fixed-seed networks across topology, size, solver configuration, reaction, source, and cancellation variants. |
| Model-field audit | Require every audited model field to name test evidence or an explicit non-EPANET exclusion. |
| Upstream inventory | Classify every vendored EPANET test as adapter evidence, native-only behavior, or not applicable. |

A default value is not evidence by itself. A numeric field is covered only when a scenario verifies a meaningful non-default value or a physical invariant that constrains it.

## Running the suite

```bash
./compile_linux_tests.sh
```

The script configures and builds the test target and runs the default CTest suite. Every scenario has its own CTest name.

```bash
ctest --test-dir build-linux-tests -N
ctest --test-dir build-linux-tests -R <scenario-name> --verbose
```

Labelled groups:

```bash
ctest --test-dir build-linux-tests -L contract --output-on-failure
ctest --test-dir build-linux-tests -L conformance --output-on-failure
ctest --test-dir build-linux-tests -L hydraulic --output-on-failure
ctest --test-dir build-linux-tests -L quality --output-on-failure
ctest --test-dir build-linux-tests -L negative --output-on-failure
ctest --test-dir build-linux-tests -L export --output-on-failure
ctest --test-dir build-linux-tests -L stress --output-on-failure
ctest --test-dir build-linux-tests -L proof --output-on-failure
ctest --test-dir build-linux-tests -L upstream --output-on-failure
```

The repository does not hard-code test totals in this document. CTest is the authoritative inventory, so adding or removing a registered scenario cannot leave a stale published count here.

## Hydraulic coverage

| Area | Verified behavior |
|---|---|
| Time and solver options | Duration; hydraulic, quality, pattern, report, and rule timing; clock start; all report statistics; DDA/PDA; convergence, damping, checking, demand, emitter, gravity, and viscosity options. |
| Junctions | Elevation forms, demand categories, pattern modes, emitters, coordinates, demand delivery and deficit, leakage attribution, head, pressure, and control membership. |
| Reservoirs | Constant and patterned head, coordinates, demand, head, pressure, and control membership. |
| Tanks | All supported geometry forms, volume curves, overflow, coordinates, level, volume, mixing-zone volume, and control membership. |
| Pipes | All three headloss formulas, formula-specific roughness, status, check valves, minor loss, FAVAD leakage, vertices, flow, velocity, headloss, gradient, friction factor, and control membership. |
| Pumps | Constant power; one-point, three-point, and multipoint curves; speed, patterns, status, efficiency, pricing, energy summaries, operating states, flow, velocity, head gain, power, and control membership. |
| Valves | PRV, PSV, PBV, FCV, TCV, GPV, and PCV configuration, curves, statuses, settings, flow, velocity, headloss, regulation state, and control membership. |
| Controls | Low/high level, timer, time of day, open/close/setting actions, disabled controls, and next-event identity. |
| Rules | IF/AND/OR premises, THEN/ELSE actions, priorities, structured serialization, enabled state, supported objects, variables, and comparison operators. |
| Run summaries | Solver statistics, demand deficiency, leakage loss, flow balance, pump energy, system energy, demand charge, and cost. |
| Lifecycle | Hydraulic stepping, steady state, warnings, errors, cancellation before results, and cancellation with partial results. |

The independently reconstructed Net1 scenario compares the complete hydraulic event sequence and every applicable result field against native EPANET. Focused scenarios cover features that Net1 does not exercise.

## Water-quality coverage

| Area | Verified behavior |
|---|---|
| Analysis selection | Disabled analysis, chemical concentration, water age, source trace, trace-node resolution, tolerance, diffusivity, and quality timestep. |
| Initial values and sources | Quantity-specific initial values; concentration, mass-booster, flow-paced, and setpoint sources; patterned dosing. |
| Tank mixing | Complete mix, two-compartment, FIFO, LIFO, mixing fraction, and tank reaction overrides. |
| Reactions | Bulk, wall, and tank orders and coefficients; entity overrides; limiting concentration; roughness correlation under all headloss formulas. |
| Lifecycle | Saved-hydraulics quality initialization, independent stepping, closure, warnings, errors, and partial cancellation. |
| Results | Junction, reservoir, tank, pipe, pump, and valve quality; configured chemical source mass flow; per-step mass-balance ratio. |
| Multiple analyses | Ordered execution, isolated equivalence, order independence, repeated source-trace runs, child failure isolation, and cancellation between children. |
| Generated stress | Chain, branch, ring, grid, and dual-source networks; multiple timestep ratios; long runs; reactions; patterns; and multiple cancellation positions. |

Generated quality references are written directly as native EPANET inputs from a fixed-seed specification. They do not pass through the adapter's INP exporter, which keeps the runtime differential independent of export formatting and rounding.

## Validation and export coverage

| Area | Verified behavior |
|---|---|
| Identity | Duplicate node, link, curve, and UUID detection. |
| References | Missing or disabled nodes, links, patterns, curves, controls, rules, quality sources, trace nodes, and map-label anchors. |
| Numeric validation | Non-finite, negative, out-of-range, and structurally invalid entity, curve, solver, report, control, map, and quality values. |
| Diagnostics | Multiple independent failures, stable entity identity, generic stage/operation/property, and native backend provenance. |
| Enabled state | Disabled network entities are omitted; disabled controls and rules retain membership without executing; report selections are pruned consistently. |
| Metadata | Titles, node/link comments and tags, pattern and curve comments, and generic curves. |
| Geometry | Node coordinates, ordered link vertices, map labels, and backdrop bounds in WGS84 degrees. |
| Reports | General flags, statistics, selected entities, typed fields, precision, thresholds, and backend command overrides. |

## Numeric comparisons

The canonical tolerance catalog is implemented in `tests/conformance/conformance_test_framework.cpp`. Numeric comparisons use:

```text
absolute difference <= absolute tolerance + relative tolerance * max(abs(actual), abs(expected))
```

| Quantity | Absolute tolerance | Relative tolerance |
|---|---:|---:|
| Dimensionless | `1e-9` | `1e-7` |
| Flow, m³/h | `1e-6` | `1e-6` |
| Head, pressure, or length, m | `1e-7` | `1e-6` |
| Velocity, m/s | `1e-8` | `1e-6` |
| Volume, m³ | `1e-6` | `1e-6` |
| Power or energy | `1e-6` | `1e-6` |
| Cost | `1e-8` | `1e-6` |
| Percent | `1e-6` | `1e-6` |
| Friction factor | `1e-10` | `1e-6` |
| Context-dependent setting | `1e-7` | `1e-6` |

Scenario-specific tolerance changes must be declared at the assertion and justified by native precision or numerical conditioning. Enums, booleans, identifiers, counts, and timestamps are compared exactly.

Mismatch messages identify the scenario, timestep, entity type and stable ID, field, expected value, actual value, and effective tolerance.

## Model-field evidence policy

`tests/model_field_conformance_policy.txt` inventories the supported hydraulic and water-quality model headers. Each field must be classified as:

- `complete`, with one or more registered evidence scenarios;
- `excluded-non-epanet`, with a reason explaining why it is application metadata rather than solver input;
- `excluded-runtime-metadata`, with a reason explaining why it is unstable backend-local state.

`aowis-server-epanet-model-field-conformance-audit`, implemented by `tests/conformance/verify_model_field_conformance.cmake`, compares that policy with the model headers and the registered scenario manifest. It fails when a field or struct appears without policy, policy references a removed model field, or evidence names an unregistered scenario.

## Upstream EPANET tests

`tests/upstream_test_inventory.txt` records the disposition of every vendored upstream test. Every `wrapper-candidate` row names one or more registered AOWIS conformance scenarios as executable evidence; the inventory verifier fails if an evidence scenario is missing from the registered scenario manifest. Toolkit-only CRUD and binary-output operations remain classified as native-only.

The original Boost suite can be exposed as an additional CTest entry:

```bash
cmake -S . -B build-linux-tests \
  -DAOWIS_SERVER_EPANET_BUILD_TESTS=ON \
  -DAOWIS_SERVER_EPANET_ENABLE_UPSTREAM_BOOST_TESTS=ON
cmake --build build-linux-tests --parallel
ctest --test-dir build-linux-tests -L upstream --output-on-failure
```

## Coverage reports

With GCC and `gcovr` available:

```bash
cmake -S . -B build-linux-coverage \
  -DAOWIS_SERVER_EPANET_BUILD_TESTS=ON \
  -DAOWIS_SERVER_EPANET_ENABLE_COVERAGE=ON
cmake --build build-linux-coverage --target aowis-server-epanet-coverage
```

Reports are written below `build-linux-coverage/coverage`.

## Acceptance rule

The adapter conformance claim is valid when:

1. the default CTest suite passes;
2. the scenario-manifest, upstream-inventory, and model-field audit tests pass;
3. every supported input and result family has non-default evidence;
4. every excluded field and upstream test has an explicit technical reason;
5. no supported configuration is accepted as a silent no-op.

The optional upstream Boost suite and source coverage reports provide additional release evidence but do not redefine the public adapter contract.
