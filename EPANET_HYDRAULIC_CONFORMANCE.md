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

Roadmap step 3 is implemented in the suite. In addition to the step-2 Net1 differential baseline, individually runnable upstream-derived scenarios now cover DDA/PDA stress behavior, FAVAD leakage, tank overflow, PCV characteristic curves, demand/default-pattern assignment, replacement simple controls, and explicit hydraulic stepping. AOWIS intentionally exposes only its canonical metric hydraulic unit contract, so alternate EPANET user-unit modes are not part of wrapper conformance.

Each step-3 scenario runs the relevant vendored EPANET case natively and through an independently configured AOWIS model, retains the useful upstream golden value or physical invariant, and then compares every applicable non-quality hydraulic result field at every event. This materially broadens differential evidence beyond nominal Net1 behavior, but it does **not** yet prove complete hydraulic conformance: the systematic node/pipe, pump/energy, valve, controls/options, and fidelity-hardening phases remain.

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
| `aowis-server-epanet-conformance-upstream-pcv` | Differential conformance | PCV position curve and 35%-open head-loss golden with complete valve/result comparison |
| `aowis-server-epanet-conformance-upstream-demand-pattern` | Differential conformance | Default-pattern identity plus explicit demand-pattern factors/assignment and complete timeline comparison |
| `aowis-server-epanet-conformance-upstream-simple-control` | Differential conformance | Upstream replacement low/high controls, final tank-head invariant, event identity, and complete timeline comparison |
| `aowis-server-epanet-conformance-upstream-hydraulic-stepping` | Differential conformance | Explicit EN_runH/EN_nextH monotonic stepping and all intermediate event boundaries compared against the wrapper |
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
| I-OPTIONS-TIME | Duration and timing | duration, hydraulic/report/pattern/rule timesteps, pattern/report start, start clock time | `EN_settimeparam` | 2, 7 | Net1 event timeline plus timer and steady-state contracts | Native compared (Net1 subset) |
| I-OPTIONS-HYDRAULIC | Solver configuration | headloss formula, demand model and limits, accuracy, trials, damping, status and convergence checks, demand multiplier, emitter exponent, specific gravity, viscosity, unbalanced behavior | `EN_setoption`, `EN_setdemandmodel` | 3, 4, 7 | Net1 H-W/DDA solver configuration | Native compared (Net1 subset) |
| I-OPTIONS-ENERGY | Energy configuration | global efficiency and price, global price pattern, demand charge | `EN_setoption` energy options | 5 | Pump energy relationship scenarios | Contract |
| I-OPTIONS-REPORT | Report configuration | page, status, summary, messages, energy, node/link selection, typed fields, backend commands | `EN_setreport` and report selection APIs | 7, 8 | None differential | Inventory |
| I-PATTERN | Time patterns | IDs, factors, default demand pattern, demand/head/speed/price references | pattern Toolkit APIs | 2, 3, 5 | Net1 12-factor default demand pattern | Native compared (Net1 subset) |
| I-CURVE-TANK | Tank volume curves | IDs, level points, volume points, validation | curve Toolkit APIs | 4 | None differential | Inventory |
| I-CURVE-PUMP | Pump head and efficiency curves | one-point, three-point, multipoint, library curves, efficiency curves | curve Toolkit APIs and pump curve properties | 5 | Net1 one-point curve plus three-point pump contract | Native compared (one-point) |
| I-CURVE-VALVE | Valve curves | GPV head-loss and PCV characteristic curves | curve Toolkit APIs and valve curve properties | 6 | None differential | Inventory |
| I-JUNCTION | Junction inputs | elevation, demand categories, constant/pattern mode, emitter, enabled state, coordinates | node and demand Toolkit APIs | 2, 4 | Nine patterned Net1 junctions plus demand/leakage contract | Native compared (Net1 subset) |
| I-RESERVOIR | Reservoir inputs | head, constant/pattern mode, enabled state, coordinates | node Toolkit APIs | 2, 4 | Net1 fixed-head reservoir | Native compared (Net1 subset) |
| I-TANK | Tank inputs | elevation, levels, diameter, volume forms, curve, overflow, enabled state, coordinates | tank node Toolkit APIs | 3, 4 | Net1 cylindrical tank with operating levels | Native compared (cylindrical subset) |
| I-PIPE | Pipe inputs | endpoints, length, diameter, formula-specific roughness, minor loss, status/check valve, leakage, vertices, enabled state | link Toolkit APIs | 2, 3, 4 | Twelve H-W Net1 pipes plus leakage contract | Native compared (H-W subset) |
| I-PUMP | Pump inputs | endpoints, definition, curves, power, status, speed, speed pattern, efficiency, energy price/pattern, vertices, enabled state | pump link Toolkit APIs | 2, 5 | Net1 one-point, constant-speed pump plus contract fixtures | Native compared (Net1 subset) |
| I-VALVE | Valve inputs | all seven types, endpoints, diameter, minor loss, status, setting, GPV/PCV curves, vertices, enabled state | valve link Toolkit APIs | 3, 6 | None differential | Inventory |
| I-CONTROL-SIMPLE | Simple controls | low/high level, timer, time of day, open/close/setting, enabled state | control Toolkit APIs | 3, 7 | Net1 low/high tank-level pump controls plus timer and disabled contracts | Native compared (level-control subset) |
| I-CONTROL-RULE | Structured rules | IF/AND/OR, object/variable/operator/value/status, THEN/ELSE, priority, source text, enabled state | rule Toolkit APIs | 3, 7 | Time-rule and unsupported-POWER fixtures | Contract |
| I-PREPARATION | Snapshot preparation | disabled entity removal, control retention, invalid/disabled references, selected report entities | adapter preparation plus native counts | 7, 8 | Disabled-control membership fixture | Contract |
| I-METADATA | Export and geometry fidelity | titles, comments, tags, generic curves, coordinates, vertices, map positions | metadata, coordinate, vertex, and INP APIs | 8 | Vertices implemented but not tested here | Inventory |

## Initial result matrix

Water-quality members are intentionally omitted from this hydraulic matrix.

| ID | Result family | Fields requiring native comparison | Planned phase | Current evidence | State |
|---|---|---|---:|---|---|
| R-TIMELINE | Timeline | validity, status, elapsed time, timestep sequence, cancellation, partial-result behavior | 2, 7 | Full native-versus-wrapper Net1 event sequence | Native compared (successful run) |
| R-EVENT | Next event | type, time until event, tank ID/UUID, control ID/UUID | 2, 7 | Every Net1 event, including stable AOWIS IDs/UUIDs | Native compared (Net1) |
| R-JUNCTION | Junction | ID/UUID, requested demand, delivered demand, deficit, total demand, emitter flow, leakage flow, head, pressure head, control membership | 2, 3, 4 | Every Net1 junction field at every event; original golden values retained | Native compared (Net1) |
| R-RESERVOIR | Reservoir | ID/UUID, net demand, head, pressure head, control membership | 2, 4 | Every Net1 reservoir field at every event | Native compared (Net1) |
| R-TANK | Tank | ID/UUID, net demand, head, pressure head, level, volume, mixing-zone volume, control membership | 2, 3, 4 | Every Net1 tank field at every event; original golden values retained | Native compared (Net1) |
| R-PIPE | Pipe | ID/UUID, flow, leakage, velocity, head loss, unit head loss, formula-specific roughness, friction factor, open state, control membership | 2, 3, 4 | Every Net1 pipe field at every event | Native compared (Net1) |
| R-PUMP | Pump | ID/UUID, flow, velocity, head gain, open state, operating state, speed, efficiency, power, control membership | 2, 5 | Every Net1 pump field at every event | Native compared (Net1) |
| R-VALVE | Valve | ID/UUID, flow, velocity, head loss, open state, active state, setting, control membership | 3, 6 | None native | Inventory |
| R-STATISTICS | Hydraulic statistics | iterations, relative error, maximum head error, maximum flow change, deficient nodes, demand reduction, leakage loss | 3, 7 | Every non-quality Net1 statistic at every event | Native compared (Net1) |
| R-PUMP-ENERGY | Per-pump energy | pump ID/UUID, time online, average efficiency, average kW per flow unit, average power, peak power, average daily cost | 5 | Independently accumulated Net1 pump summary plus contracts | Native compared (Net1) |
| R-SYSTEM-ENERGY | System energy | energy cost per day, peak power, demand charge per day, total cost per day | 5 | Independently accumulated Net1 system summary plus contracts | Native compared (Net1) |
| R-FLOW-BALANCE | Run flow balance | total inflow/outflow, consumer demand, demand deficit, emitter flow, leakage flow, storage flow, balance ratio | 3, 4, 7 | Independently accumulated Net1 full-run balance plus contracts | Native compared (Net1) |
| R-DIAGNOSTICS | Diagnostics | stage, operation, entity, property, backend operation/code/message, warning/error validity | 7, 8 | Unsupported pump-POWER diagnostic | Contract |

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

The next implementation step is roadmap step 4: systematically exercise the remaining non-default junction, reservoir, tank, pipe, demand-category, tank-volume-curve, and formula-specific pipe input mappings through both paths.
