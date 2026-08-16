# EPANET Hydraulic Conformance

This document is the audit ledger for proving that the AOWIS model-driven EPANET adapter delivers native EPANET hydraulic behavior and results.

## Scope

Included:

- Model-to-EPANET hydraulic input mapping.
- Hydraulic execution through `EpanetRunner`.
- Every hydraulic timestep and event boundary.
- Junction, reservoir, tank, pipe, pump, valve, control, statistic, energy, and flow-balance results.
- Diagnostics, invalid input, enabled state, and INP fidelity.

Excluded from this conformance denominator for now:

- Water-quality execution and results.
- Chemical, age, trace, source, reaction, and tank-mixing quality configuration.
- Native Toolkit project-handle CRUD and binary Output API functions that the high-level wrapper does not expose.

An excluded field is not treated as covered. Water quality will receive its own conformance expansion later.

## Current claim

Roadmap step 7 is implemented in the suite. Controls, timing, hydraulic solver options, report statistics, event identification, operational statistics/flow balance, warnings/errors, and cancellation/partial-result semantics now have independently named conformance scenarios. The structured-rule suite covers IF/AND/OR, THEN/ELSE, priority, enabled state, status and setting actions, every supported premise object-variable combination, and every supported comparison operator. Pump `POWER` premises remain explicitly rejected by their independent negative contract test because the bundled EPANET 2.3 rule engine cannot execute that premise.

Steps 5 and 6 remain complete for pumps and all seven valve types. Complete hydraulic conformance is still not claimed: preparation/fidelity hardening, metadata/geometry fidelity, disabled entity preparation, and remaining INP/report fidelity paths are later phases.

## Running the tests

Build and run every adapter test:

```bash
./compile_linux_tests.sh
```

List the individually registered tests:

```bash
ctest --test-dir build-linux-tests -N
```

Run one scenario with full output:

```bash
ctest --test-dir build-linux-tests \
  -R '^aowis-server-epanet-conformance-net1$' \
  --verbose
```

List the scenario registry directly:

```bash
./build-linux-tests/tests/aowis-server-epanet-conformance-tests --list
```

Run all scenarios through the executable without CTest:

```bash
./build-linux-tests/tests/aowis-server-epanet-conformance-tests --all
```

## Current named scenarios

Step 7 adds 86 independently registered scenarios. The test names are grouped by the following stable prefixes; every listed branch has its own CTest entry rather than being hidden inside one aggregate test:

| Prefix/family | Independently named branches |
|---|---|
| `conformance-controls-simple-*` / `conformance-controls-action-*` | low-level, high-level, timer, time-of-day, open, close, setting, disabled |
| `conformance-controls-rule-*` | IF, AND, OR, THEN, ELSE, setting action, ACTIVE action, preserved source text, priority, disabled |
| `conformance-controls-premise-*` | node demand/head/grade/level/pressure/fill-time/drain-time; link flow/status/setting; system demand/time/clock-time |
| `conformance-controls-operator-*` | equal, not-equal, <=, >=, <, >, IS, IS NOT, BELOW, ABOVE |
| `conformance-controls-*-event*` | next hydraulic event and stable control-event ID/UUID identification |
| `conformance-options-time-*` | duration, hydraulic/quality/pattern/report/rule timestep, pattern/report start, start clock |
| `conformance-options-report-statistic-*` | series, average, minimum, maximum, range |
| `conformance-options-hydraulic-*` | every supported hydraulic solver option branch, including DDA/PDA and both unbalanced behaviors |
| `conformance-operational-*` | statistics, flow balance, warning diagnostics, error diagnostics, cancellation-before-results, cancellation-with-partial-results |
| `contract-reject-pump-power-rule` | explicit unsupported pump `POWER` premise rejection |

| CTest name | Kind | What it currently establishes |
|---|---|---|
| `aowis-server-epanet-contract-physical-results-and-leakage` | Contract | Nonzero FAVAD leakage, node/pipe allocation, control membership, derived head loss, friction, and flow-balance relationships |
| `aowis-server-epanet-contract-structured-rule-control` | Contract | A structured time rule closes a pipe and control membership is returned |
| `aowis-server-epanet-contract-timer-pump-energy` | Contract | Timer event reporting, pump closure, time online, peak power, and cost relationships |
| `aowis-server-epanet-contract-reject-pump-power-rule` | Negative contract | Pump `POWER` premises are rejected explicitly because the bundled rule engine cannot execute them |
| `aowis-server-epanet-contract-steady-state-pump-energy` | Regression | A steady-state run returns one timestep, pump energy, and a closed flow balance |
| `aowis-server-epanet-conformance-net1` | Differential conformance | Native upstream Net1 and its independently constructed AOWIS equivalent match at every hydraulic event; original upstream 10,800-second golden values also pass |
| `aowis-server-epanet-conformance-upstream-dda-pda` | Differential conformance | Upstream DDA warning/deficient-node behavior and PDA demand reduction/deficit goldens, followed by complete native-wrapper comparisons |
| `aowis-server-epanet-conformance-upstream-leakage` | Differential conformance | Upstream FAVAD pipe leakage, node conservation, independent formula check, and complete native-wrapper comparison |
| `aowis-server-epanet-conformance-upstream-tank-overflow` | Differential conformance | Overflow disabled/enabled behavior, full-tank and spillage/inflow invariants, and complete native-wrapper comparisons |
| `aowis-server-epanet-conformance-upstream-pcv` | Differential conformance | PCV non-default diameter/minor loss, Active status, 35%-open characteristic curve, returned setting, upstream head-loss golden, and complete valve-result comparison |
| `aowis-server-epanet-conformance-upstream-demand-pattern` | Differential conformance | Default-pattern identity plus explicit demand-pattern factors/assignment and complete timeline comparison |
| `aowis-server-epanet-conformance-upstream-simple-control` | Differential conformance | Upstream replacement low/high controls, final tank-head invariant, event identity, and complete timeline comparison |
| `aowis-server-epanet-conformance-upstream-hydraulic-stepping` | Differential conformance | Explicit EN_runH/EN_nextH monotonic stepping and all intermediate event boundaries compared against the wrapper |
| `aowis-server-epanet-conformance-upstream-junction-reservoir-inputs` | Differential conformance | Terrain-plus-offset junction elevation, emitter input, patterned demand, and patterned reservoir head compared through native and wrapper paths |
| `aowis-server-epanet-conformance-upstream-demand-categories` | Differential conformance | Multiple independently named demand categories with patterned and constant modes; proves constant categories remain constant under a global default pattern |
| `aowis-server-epanet-conformance-upstream-tank-uniform-area` | Differential conformance | Uniform-area tank geometry, terrain-plus-offset bottom elevation, minimum volume, and initial hydraulic volume/head mapping |
| `aowis-server-epanet-conformance-upstream-tank-volume-at-max` | Differential conformance | Volume-at-maximum-level tank geometry resolves to the equivalent native tank and matches initial volume/head |
| `aowis-server-epanet-conformance-upstream-tank-volume-curve` | Differential conformance | Non-uniform tank level-volume curve, nonzero minimum volume, and initial curve-interpolated volume compared natively |
| `aowis-server-epanet-conformance-upstream-pipe-inputs` | Differential conformance | Measured length, diameter, Hazen-Williams roughness, minor loss, reversed check valve, and initial closed status mappings |
| `aowis-server-epanet-conformance-upstream-pipe-darcy-weisbach` | Differential conformance | Darcy-Weisbach selection and millimetre absolute roughness mapping with complete hydraulic result comparison |
| `aowis-server-epanet-conformance-upstream-pipe-chezy-manning` | Differential conformance | Chezy-Manning selection and Manning roughness mapping with complete hydraulic result comparison |
| `aowis-server-epanet-conformance-upstream-pump-three-point` | Differential conformance | Three-point pump head definition and complete instantaneous pump-result mapping |
| `aowis-server-epanet-conformance-upstream-pump-multipoint` | Differential conformance | Four-point custom pump curve and independent piecewise head interpolation invariant |
| `aowis-server-epanet-conformance-upstream-pump-constant-power` | Differential conformance | Constant hydraulic-power definition and electrical power/efficiency relationship |
| `aowis-server-epanet-conformance-upstream-pump-initial-speed` | Differential conformance | Non-default initial relative speed and its hydraulic result mapping |
| `aowis-server-epanet-conformance-upstream-pump-speed-pattern` | Differential conformance | Pattern-driven speed changes at successive hydraulic pattern boundaries |
| `aowis-server-epanet-conformance-upstream-pump-initial-off` | Differential conformance | Initial Off status, zero power, and Closed operating state |
| `aowis-server-epanet-conformance-upstream-pump-constant-efficiency` | Differential conformance | Pump-specific constant efficiency implemented through native efficiency-curve semantics |
| `aowis-server-epanet-conformance-upstream-pump-efficiency-curve` | Differential conformance | Pump efficiency-curve assignment and independent interpolation invariant |
| `aowis-server-epanet-conformance-upstream-pump-global-energy` | Differential conformance | Global efficiency, patterned energy price, demand charge, and pump/system energy summaries |
| `aowis-server-epanet-conformance-upstream-pump-energy-pattern` | Differential conformance | Pump-specific energy price and price-pattern override against native EPANET properties |
| `aowis-server-epanet-conformance-upstream-pump-xhead` | Differential conformance | `CannotSupplyHead` / `XHEAD` operating state and closed mapping |
| `aowis-server-epanet-conformance-upstream-pump-xflow` | Differential conformance | `CannotSupplyFlow` / `XFLOW` operating state and open mapping |
| `aowis-server-epanet-conformance-upstream-valve-prv` | Differential conformance | PRV pressure regulation with non-default diameter/minor loss, Active status, returned setting, and complete valve-result mapping |
| `aowis-server-epanet-conformance-upstream-valve-psv` | Differential conformance | PSV pressure sustaining with non-default diameter/minor loss, Active status, returned setting, and complete valve-result mapping |
| `aowis-server-epanet-conformance-upstream-valve-pbv` | Differential conformance | PBV fixed pressure-breaker head loss, non-default geometry/loss inputs, Active status, and returned setting |
| `aowis-server-epanet-conformance-upstream-valve-fcv` | Differential conformance | FCV exact controlled-flow behavior, non-default geometry/loss inputs, Active status, and returned setting |
| `aowis-server-epanet-conformance-upstream-valve-tcv` | Differential conformance | TCV throttling setting, minor loss, non-default diameter, Active status, and complete valve-result mapping |
| `aowis-server-epanet-conformance-upstream-valve-gpv` | Differential conformance | GPV non-linear head-loss curve interpolation, explicit Open initial status, non-default diameter/minor loss, and returned curve setting |
| `aowis-server-epanet-test-scenario-manifest` | Framework | Every C++ scenario is individually registered in CTest and vice versa |
| `aowis-server-epanet-upstream-test-inventory` | Framework | Every active vendored upstream source-level test has an explicit classification |

## Upstream EPANET inventory

`tests/upstream_test_inventory.txt` is the machine-readable classification. Its CTest verifier scans the vendored EPANET test sources and fails when a test is added, removed, duplicated, or left unclassified.

The vendored source currently contains:

| Classification | Count | Meaning |
|---|---:|---|
| Wrapper candidate | 39 | Rebuild as an AOWIS model scenario, retain useful upstream golden assertions, and compare native versus wrapper behavior |
| Native-only | 39 | Keep in the upstream baseline because it tests Toolkit CRUD, handles, files, internal utilities, arbitrary native unit modes, or binary Output API behavior not exposed by the wrapper |
| Not applicable now | 7 | Water-quality behavior or a quality-dependent path excluded from the current hydraulic scope |
| **Total active source-level cases** | **85** | 84 active Boost cases plus the standalone re-entrancy program |

Two additional Boost cases are commented out upstream and are not active tests:

- `external/epanet/tests/test_net_builder.cpp`: `net_builder_I`
- `external/epanet/tests/outfile/test_output.cpp`: `AccessTest`

The source-level count is deliberately separate from upstream CTest's executable count. Upstream groups many Boost cases into a few executables, and it builds the C-string helper test without registering it with CTest.

## Evidence states

Every matrix row advances through these states:

| State | Required evidence |
|---|---|
| Inventory | The field or behavior has a row and an owning phase |
| Contract | A wrapper-only assertion verifies a non-default relationship or explicit diagnostic |
| Native compared | Native EPANET and the wrapper match at every applicable timestep with an independently normalized reference |
| Golden checked | At least one independent expected value or physical invariant also passes |
| Complete | Input mapping, result mapping, non-default behavior, negative behavior where relevant, and documentation are all present |

`0.0 == 0.0` is not coverage unless zero is the meaningful expected behavior and a paired scenario proves the field changes under a non-default input.

## Initial input matrix

| ID | Contract area | Hydraulic fields or behavior | Native EPANET path | Planned phase | Current evidence | State |
|---|---|---|---|---:|---|---|
| I-OPTIONS-TIME | Duration and timing | duration, hydraulic/quality/report/pattern/rule timesteps, pattern/report start, start clock time | `EN_settimeparam` | 2, 7 | Every Model timing field has an independent native readback scenario; timer/time-of-day execution also verifies event timing | Complete |
| I-OPTIONS-HYDRAULIC | Solver configuration | headloss formula, demand model and limits, accuracy, trials, damping, status and convergence checks, demand multiplier/default pattern, emitter exponent/backflow, specific gravity, viscosity, unbalanced behavior | `EN_setoption`, `EN_setdemandmodel` | 3, 4, 7 | Every supported solver-option branch has an independent native readback test; all three headloss formulas are covered across steps 4 and 7 | Complete |
| I-OPTIONS-ENERGY | Energy configuration | global efficiency and price, global price pattern, demand charge | `EN_setoption` energy options | 5 | Global-efficiency/global-price-pattern/demand-charge differential scenario with independent native cost accumulation | Complete |
| I-OPTIONS-REPORT | Report configuration | page, status, summary, messages, energy, node/link selection, typed fields, backend commands | `EN_setreport` and report selection APIs | 7, 8 | None differential | Inventory |
| I-PATTERN | Time patterns | IDs, factors, default demand pattern, demand/head/speed/price references | pattern Toolkit APIs | 2, 3, 5 | Demand, reservoir-head, pump-speed, global energy-price, and pump energy-price patterns exercised differentially | Native compared (hydraulic references) |
| I-CURVE-TANK | Tank volume curves | IDs, level points, volume points, validation | curve Toolkit APIs | 4 | Five-point non-uniform level-volume curve used by tank-volume-curve differential scenario | Native compared |
| I-CURVE-PUMP | Pump head and efficiency curves | one-point, three-point, multipoint head curves, efficiency curves | curve Toolkit APIs and pump curve properties | 5 | One-point Net1 plus three-point, four-point custom, constant-efficiency, and multipoint efficiency curves | Complete |
| I-CURVE-VALVE | Valve curves | GPV head-loss and PCV characteristic curves | curve Toolkit APIs and valve curve properties | 6 | Independent GPV head-loss and PCV characteristic-curve valve scenarios | Complete |
| I-JUNCTION | Junction inputs | elevation, demand categories, constant/pattern mode, emitter, enabled state, coordinates | node and demand Toolkit APIs | 2, 4 | Direct and terrain-plus-offset elevation, emitter, patterned demand, and three demand categories including constant mode; enabled/coordinates remain later fidelity/preparation work | Native compared (hydraulic inputs) |
| I-RESERVOIR | Reservoir inputs | head, constant/pattern mode, enabled state, coordinates | node Toolkit APIs | 2, 4 | Fixed Net1 head plus terrain-plus-offset and patterned reservoir-head scenario; enabled/coordinates remain later work | Native compared (hydraulic inputs) |
| I-TANK | Tank inputs | elevation, levels, diameter, volume forms, curve, overflow, enabled state, coordinates | tank node Toolkit APIs | 3, 4 | Cylindrical Net1 plus uniform-area, volume-at-maximum, and volume-curve geometries; nonzero minimum volume, terrain-plus-offset bottom elevation, and overflow covered | Native compared (hydraulic inputs) |
| I-PIPE | Pipe inputs | endpoints, length, diameter, formula-specific roughness, minor loss, status/check valve, leakage, vertices, enabled state | link Toolkit APIs | 2, 3, 4 | H-W Net1 plus measured length, diameter, minor loss, reversed CV, closed status, FAVAD leakage, and D-W/C-M roughness scenarios; vertices/enabled remain later work | Native compared (hydraulic inputs) |
| I-PUMP | Pump inputs | endpoints, definition, curves, power, status, speed, speed pattern, efficiency, energy price/pattern, vertices, enabled state | pump link Toolkit APIs | 2, 5 | All hydraulic pump definitions, status/speed modes, efficiency modes, and energy pattern/override paths are native-compared; vertices/enabled remain phase-8 preparation/fidelity concerns | Native compared (complete hydraulic inputs) |
| I-VALVE | Valve inputs | all seven types, endpoints, diameter, minor loss, status, setting, GPV/PCV curves, vertices, enabled state | valve link Toolkit APIs | 3, 6 | PRV/PSV/PBV/FCV/TCV/GPV plus the upstream PCV case are native-compared for hydraulic inputs, settings/status, both valve curve types, and complete valve results; vertices/enabled remain phase-8 fidelity concerns | Native compared (complete hydraulic inputs) |
| I-CONTROL-SIMPLE | Simple controls | low/high level, timer, time of day, open/close/setting, enabled state | control Toolkit APIs | 3, 7 | Every simple-control type, action, and enabled-state branch has its own native readback/execution scenario | Complete |
| I-CONTROL-RULE | Structured rules | IF/AND/OR, object/variable/operator/value/status, THEN/ELSE, priority, source text, enabled state | rule Toolkit APIs | 3, 7 | IF/AND/OR; THEN/ELSE; status/setting/ACTIVE actions; preserved source text; priority conflict resolution; disabled rules; every supported object-variable and comparison operator; explicit POWER rejection | Complete |
| I-PREPARATION | Snapshot preparation | disabled entity removal, control retention, invalid/disabled references, selected report entities | adapter preparation plus native counts | 7, 8 | Disabled-control membership fixture | Contract |
| I-METADATA | Export and geometry fidelity | titles, comments, tags, generic curves, coordinates, vertices, map positions | metadata, coordinate, vertex, and INP APIs | 8 | Vertices implemented but not tested here | Inventory |

## Initial result matrix

Water-quality members are intentionally omitted from this hydraulic matrix.

| ID | Result family | Fields requiring native comparison | Planned phase | Current evidence | State |
|---|---|---|---:|---|---|
| R-TIMELINE | Timeline | validity, status, elapsed time, timestep sequence, cancellation, partial-result behavior | 2, 7 | Full native Net1 timeline plus independent cancellation-before-results and mid-run partial-result scenarios | Complete |
| R-EVENT | Next event | type, time until event, tank ID/UUID, control ID/UUID | 2, 7 | Full Net1 event sequence plus dedicated next-control-event timing and stable control ID/UUID scenarios | Complete |
| R-JUNCTION | Junction | ID/UUID, requested demand, delivered demand, deficit, total demand, emitter flow, leakage flow, head, pressure head, control membership | 2, 3, 4 | Net1 plus DDA/PDA, leakage, emitter, and multi-category demand scenarios compare every applicable junction result | Native compared (expanded) |
| R-RESERVOIR | Reservoir | ID/UUID, net demand, head, pressure head, control membership | 2, 4 | Net1 plus patterned-head reservoir scenario compare every reservoir result across events | Native compared (expanded) |
| R-TANK | Tank | ID/UUID, net demand, head, pressure head, level, volume, mixing-zone volume, control membership | 2, 3, 4 | Net1, overflow, and all AOWIS tank geometry input modes exercise tank result mapping | Native compared (expanded) |
| R-PIPE | Pipe | ID/UUID, flow, leakage, velocity, head loss, unit head loss, formula-specific roughness, friction factor, open state, control membership | 2, 3, 4 | Net1 plus leakage, non-default pipe inputs, check-valve/closed status, and H-W/D-W/C-M formula scenarios | Native compared (expanded) |
| R-PUMP | Pump | ID/UUID, flow, velocity, head gain, open state, operating state, speed, efficiency, power, control membership | 2, 5 | Every pump result field plus all four EPANET operating states, patterned speed, efficiency modes, and constant-power behavior | Complete |
| R-VALVE | Valve | ID/UUID, flow, velocity, head loss, open state, active state, setting, control membership | 3, 6 | All seven EPANET/AOWIS valve types are independently native-compared, including regulating, sustaining, fixed-drop, flow-control, throttling, GPV-curve, and PCV-position-curve behavior | Complete |
| R-STATISTICS | Hydraulic statistics | iterations, relative error, maximum head error, maximum flow change, deficient nodes, demand reduction, leakage loss | 3, 7 | Full native comparison plus dedicated statistics-population scenario; DDA/PDA/leakage cases cover the nonzero specialized fields | Complete |
| R-PUMP-ENERGY | Per-pump energy | pump ID/UUID, time online, average efficiency, average kW per flow unit, average power, peak power, average daily cost | 5 | Independently accumulated native summaries with global and pump-specific patterned prices plus contracts | Complete |
| R-SYSTEM-ENERGY | System energy | energy cost per day, peak power, demand charge per day, total cost per day | 5 | Independently accumulated patterned-cost and demand-charge differential scenarios plus contracts | Complete |
| R-FLOW-BALANCE | Run flow balance | total inflow/outflow, consumer demand, demand deficit, emitter flow, leakage flow, storage flow, balance ratio | 3, 4, 7 | Full native comparison plus dedicated balance sanity scenario and nonzero demand-deficit/leakage coverage | Complete |
| R-DIAGNOSTICS | Diagnostics | stage, operation, entity, property, backend operation/code/message, warning/error validity | 7, 8 | Dedicated warning and pre-simulation error scenarios plus explicit unsupported pump-POWER diagnostic; deeper report/metadata fidelity remains phase 8 | Native/contract covered for operational warning/error branches |

## Numeric tolerance catalog

The canonical catalog lives in `tests/conformance/conformance_test_framework.cpp`. A comparison passes when:

```text
absolute difference <= absolute tolerance + relative tolerance * max(abs(actual), abs(expected))
```

| Quantity | Absolute | Relative |
|---|---:|---:|
| Dimensionless | `1e-9` | `1e-7` |
| Flow, m³/h | `1e-6` | `1e-6` |
| Head, pressure head, or length, m | `1e-7` | `1e-6` |
| Velocity, m/s | `1e-8` | `1e-6` |
| Volume, m³ | `1e-6` | `1e-6` |
| Power, kW | `1e-6` | `1e-6` |
| Energy, kWh | `1e-6` | `1e-6` |
| Cost | `1e-8` | `1e-6` |
| Percent | `1e-6` | `1e-6` |
| Friction factor | `1e-10` | `1e-6` |
| Context-dependent setting | `1e-7` | `1e-6` |

Scenario-specific overrides must be explicit at the assertion and justified by native precision, numerical conditioning, or a documented golden-value precision. Exact enums, booleans, IDs, counts, and timestamps use exact comparisons.

## Required mismatch diagnostics

Numeric and exact field failures identify:

- Scenario.
- Hydraulic time when applicable.
- Entity type and stable entity ID when applicable.
- Field.
- Expected and actual values.
- Difference and tolerance for numeric values.
- A human-readable assertion message when supplied.

## Completion rule

A hydraulic-complete claim is allowed only when every included row is `Complete`, every supported Model field has a field-level child row, every numeric result is exercised non-default, every useful upstream candidate has a disposition and scenario, and both upstream EPANET tests and wrapper conformance tests pass.

The next implementation step is roadmap step 8: preparation/fidelity hardening, disabled-entity behavior, metadata/geometry fidelity, and remaining INP/report fidelity paths.
