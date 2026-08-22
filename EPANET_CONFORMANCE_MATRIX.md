# EPANET Conformance Matrix

This document is the detailed evidence matrix for the AOWIS model-driven EPANET 2.3 adapter. It complements `EPANET_CONFORMANCE.md` by mapping supported model inputs, INP import behavior, execution behavior, results, validation paths, export behavior, and upstream EPANET tests to concrete registered scenarios.

## Scope

The matrix covers:

- model-to-EPANET hydraulic and water-quality input translation;
- hydraulic execution and event timelines;
- chemical, water-age, and source-trace execution on saved hydraulics;
- junction, reservoir, tank, pipe, pump, valve, control, statistic, energy, flow-balance, and quality results;
- diagnostics, cancellation, partial results, invalid input, and enabled-state handling;
- INP import with canonical unit conversion, explicit completeness diagnostics, and native open-error provenance;
- INP export and native reopen fidelity;
- deterministic generated-network hydraulic and quality differentials;
- machine-checked model-field evidence and the vendored upstream-test inventory.

Native Toolkit project CRUD, arbitrary project-handle manipulation, hydraulic-state files, and the public binary Output API are outside the `EpanetRunner` contract unless a specific adapter behavior depends on them.

## Coverage policy

A feature is classified as complete only when the applicable adapter behavior is backed by registered evidence. Native differential tests are preferred for solver behavior; readback, export/reopen, negative-validation, contract, and field-audit tests cover behavior that is not meaningfully expressed by a single differential result.

Hydraulic and water-quality tests use separate CTest labels where useful, but both are part of the adapter conformance target. Test counts are intentionally not recorded here; CTest is the authoritative inventory.

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

Grouped test commands:

```bash
ctest --test-dir build-linux-tests -L conformance --output-on-failure
ctest --test-dir build-linux-tests -L contract --output-on-failure
ctest --test-dir build-linux-tests -L quality --output-on-failure
ctest --test-dir build-linux-tests -L negative --output-on-failure
ctest --test-dir build-linux-tests -L import --output-on-failure
ctest --test-dir build-linux-tests -L upstream --output-on-failure
ctest --test-dir build-linux-tests -L proof --output-on-failure
```

`conformance` contains native-backed hydraulic and water-quality scenarios. `contract` contains public wrapper contract and regression scenarios. `hydraulic` and `quality` select their respective solver domains. `negative` covers invalid-input and reference hardening. `import` covers INP-to-model reconstruction and import diagnostics. `export` covers INP fidelity. `stress` covers deterministic generated-network differentials. `proof` contains the scenario-manifest, upstream-inventory, field-audit, multi-quality isolation/order checks, and independent-run reentrancy proof. `upstream` always contains the source inventory and can also include the original upstream Boost suite when enabled.

### Optional original upstream Boost suite

The vendored EPANET Boost suite is intentionally not part of the default build because it adds Boost binary-library dependencies and includes native Toolkit and binary Output API tests outside the public adapter contract. Enable it explicitly:

```bash
./compile_linux_tests.sh -DAOWIS_SERVER_EPANET_ENABLE_UPSTREAM_BOOST_TESTS=ON
ctest --test-dir build-linux-tests -L upstream --output-on-failure
```

The opt-in test configures EPANET in an isolated sub-build, builds and runs the upstream CTest suite, and also executes upstream `test_cstrhelper`, which upstream builds but does not register with CTest. Missing Boost components fail during AOWIS configuration rather than during the nested build.

### Line and branch coverage

Coverage is also opt-in and currently requires GCC plus `gcovr` in `PATH`:

```bash
./compile_linux_tests.sh \
  -DAOWIS_SERVER_EPANET_ENABLE_COVERAGE=ON \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build-linux-tests --target aowis-server-epanet-coverage
```

The coverage target clears stale `.gcda` data, reruns the contract/conformance/framework proof labels, and writes adapter-only reports under `build-linux-tests/coverage/`:

- `line-coverage.txt`
- `branch-coverage.txt`
- `coverage.html` plus the gcovr detail pages

Coverage is filtered to `src/lib`; vendored EPANET and the test harness are not counted as AOWIS adapter coverage. The coverage target reports line and branch coverage but deliberately does not invent an arbitrary percentage gate.

### Machine-checked field-level audit

`tests/model_field_conformance_policy.txt` is the model-field audit policy. `aowis-server-epanet-model-field-conformance-audit` expands it against the audited AOWIS hydraulic and water-quality Model headers and writes `build-linux-tests/tests/model_field_conformance_audit.tsv`. The verifier fails when:

- an audited Model header adds or removes a struct without an audit-policy update;
- a struct adds, removes, or renames a field without an audit-policy update;
- a field is classified more than once or not at all;
- a completed field has no evidence scenario, or its evidence scenario is no longer registered;
- an excluded field lacks an explicit exclusion reason.

## Scenario groups

Controls, solver options, and operational behavior use stable scenario families. Every listed branch has its own CTest entry rather than being hidden inside one aggregate test:

| Prefix/family | Independently named branches |
|---|---|
| `conformance-controls-simple-*` / `conformance-controls-action-*` | low-level, high-level, timer, time-of-day, open, close, setting, disabled |
| `conformance-controls-rule-*` | IF, AND, OR, THEN, ELSE, setting action, ACTIVE action, structured serialization, priority, disabled |
| `conformance-controls-premise-*` | node demand/head/grade/level/pressure/fill-time/drain-time; link flow/status/setting; system demand/time/clock-time |
| `conformance-controls-operator-*` | equal, not-equal, <=, >=, <, >, IS, IS NOT, BELOW, ABOVE |
| `conformance-controls-*-event*` | next hydraulic event and stable control-event ID/UUID identification |
| `conformance-options-time-*` | duration, hydraulic/quality/pattern/report/rule timestep, pattern/report start, start clock |
| `conformance-options-report-statistic-*` | series, average, minimum, maximum, range |
| `conformance-options-hydraulic-*` | every supported hydraulic solver option branch, including DDA/PDA and both unbalanced behaviors |
| `conformance-operational-*` | statistics, flow balance, warning diagnostics, error diagnostics, cancellation-before-results, cancellation-with-partial-results |
| `contract-reject-pump-power-rule` | explicit unsupported pump `POWER` premise rejection |

INP import uses native-open/readback scenarios and keeps import success separate from source completeness:

| Scenario | What it establishes |
|---|---|
| `conformance-import-project-globals-net1` | The vendored upstream Net1 INP opens through native EPANET and reconstructs supported project/global timing, hydraulic, energy, and report settings while explicitly reporting deferred topology/quality content |
| `conformance-import-project-globals-canonical-units` | Non-canonical EPANET source flow/pressure units are converted at the import boundary into canonical AOWIS pressure-head, head-error, and flow-change units |
| `conformance-import-open-error-diagnostic` | Native `EN_open` failures retain the input-open stage/operation, backend call, native error code/message, and a structured diagnostic |

The current importer does not claim topology, patterns/curves, controls/rules, water-quality configuration, or full report-directive fidelity. When those source families are present, `EpanetResultImport::complete` is false and structured warning diagnostics identify the omitted content.

Export fidelity uses dedicated native-reopen scenarios:

| Scenario | What it establishes |
|---|---|
| `conformance-export-native-reopen` | Generated INP reopens and solves with native EPANET; a hydraulic result matches the direct wrapper run |
| `conformance-export-titles-comments-tags` | Three title lines plus junction/reservoir/tank/pipe/pump/valve comments and tags survive native reopen |
| `conformance-export-patterns-curves` | Pattern multipliers/comments plus tank-volume, pump-head, pump-efficiency, GPV head-loss, PCV characteristic, and generic curve type/data/comments survive native reopen |
| `conformance-export-coordinates-vertices` | WGS84 node coordinates and ordered link vertices survive native reopen |
| `conformance-export-report-options` | Report statistic, general flags, selected entities, every typed node/link report field, limits/precision, and backend overrides (including F-Factor) survive export and native parsing |
| `conformance-export-multiple-quality-rejected` | INP export rejects a run request containing more than one quality analysis because one INP project has only one active quality configuration |


Negative validation and disabled-reference hardening use dedicated scenarios:

| Scenario family | What it establishes |
|---|---|
| `conformance-negative-duplicate-*` | Node/link/curve IDs are unique in their EPANET namespaces and UUIDs are globally unique across hydraulic model entities |
| `conformance-negative-broken-node-reference` | Enabled links cannot reference absent endpoint UUIDs |
| `conformance-negative-disabled-node-reference` | Enabled links cannot reference disabled endpoint nodes |
| `conformance-negative-disabled-entity-pruning` | Unreferenced disabled hydraulic entities (including stale invalid values and report selections) are intentionally omitted while the remaining network stays runnable |
| `conformance-negative-disabled-control-link-reference` / `-disabled-rule-link-reference` | Simple controls and structured rules referencing disabled links fail explicitly as disabled-reference errors |
| `conformance-negative-missing-pattern` / `-missing-curve` / `-missing-valve-curve` | Missing demand patterns, pump curves, and GPV valve curves are rejected before backend construction |
| `conformance-negative-invalid-*-numeric` | Non-finite pattern, curve, node, pipe, pump, valve, simple-control, solver-option, and typed-report inputs are rejected before EPANET receives them |
| `conformance-negative-unsupported-configuration` | Unsupported hydraulic enum/configuration values return an explicit structured failure |
| `conformance-negative-structured-diagnostics` | Validation diagnostics retain stage, operation, entity identity, unresolved UUID detail, and backend provenance |

Deterministic generated-network differential stress uses dedicated scenarios:

| Scenario | Generated topology / stress dimension |
|---|---|
| `conformance-stress-chain-small-hw` | 8-junction H-W chain |
| `conformance-stress-chain-medium-dw` | 32-junction D-W chain |
| `conformance-stress-branch-medium-hw` | 31-junction branching H-W tree |
| `conformance-stress-ring-medium-cm` | 24-junction C-M ring with cross-chords |
| `conformance-stress-grid-small-hw` | 4x4 H-W looped grid |
| `conformance-stress-grid-medium-dw` | 7x7 D-W looped grid |
| `conformance-stress-grid-large-hw` | 10x10 / 100-junction H-W grid |
| `conformance-stress-dual-source-mesh-hw` | 6x8 dual-source H-W mesh with cross-grid chords and a patterned reservoir head |

Each case uses a fixed seed, two alternating junction-demand patterns, a six-hour extended-period simulation, non-default pipe length/diameter/roughness/minor-loss values, and byte-identical regeneration checks. The native EPANET INP is written independently from the stress specification instead of being exported by AOWIS, so these tests exercise the model-to-EPANET builder rather than merely reopening an AOWIS-generated file.

| CTest name | Kind | What it currently establishes |
|---|---|---|
| `aowis-server-epanet-contract-physical-results-and-leakage` | Contract | Nonzero FAVAD leakage, node/pipe allocation, control membership, derived head loss, friction, and flow-balance relationships |
| `aowis-server-epanet-contract-structured-rule-control` | Contract | A structured time rule closes a pipe and control membership is returned |
| `aowis-server-epanet-contract-timer-pump-energy` | Contract | Timer event reporting, pump closure, time online, peak power, and cost relationships |
| `aowis-server-epanet-contract-reject-pump-power-rule` | Negative contract | Pump `POWER` premises are rejected explicitly because the bundled rule engine cannot execute them |
| `aowis-server-epanet-contract-steady-state-pump-energy` | Regression | A steady-state run returns one timestep, pump energy, and a closed flow balance |
| `aowis-server-epanet-conformance-net1` | Differential conformance | Native upstream Net1 and its independently constructed AOWIS equivalent match at every hydraulic event; original upstream 10,800-second reference values also pass |
| `aowis-server-epanet-conformance-upstream-dda-pda` | Differential conformance | Upstream DDA warning/deficient-node behavior and PDA demand reduction/deficit goldens, followed by complete native-wrapper comparisons |
| `aowis-server-epanet-conformance-upstream-leakage` | Differential conformance | Upstream FAVAD pipe leakage, node conservation, independent formula check, and complete native-wrapper comparison |
| `aowis-server-epanet-conformance-upstream-tank-overflow` | Differential conformance | Overflow disabled/enabled behavior, full-tank and spillage/inflow invariants, and complete native-wrapper comparisons |
| `aowis-server-epanet-conformance-upstream-pcv` | Differential conformance | PCV non-default diameter/minor loss, Active status, 35%-open characteristic curve, returned setting, upstream head-loss reference, and complete valve-result comparison |
| `aowis-server-epanet-conformance-upstream-demand-pattern` | Differential conformance | Default-pattern identity plus explicit demand-pattern multipliers/assignment and complete timeline comparison |
| `aowis-server-epanet-conformance-upstream-simple-control` | Differential conformance | Upstream replacement low/high controls, final tank-head invariant, event identity, and complete timeline comparison |
| `aowis-server-epanet-conformance-upstream-hydraulic-stepping` | Differential conformance | Explicit EN_runH/EN_nextH monotonic stepping and all intermediate event boundaries compared against the wrapper |
| `aowis-server-epanet-conformance-upstream-junction-reservoir-inputs` | Differential conformance | Terrain-plus-offset junction elevation, dedicated emitter coefficient/exponent relation, patterned demand, and patterned reservoir head compared through native and wrapper paths |
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
| `aowis-server-epanet-conformance-reentrancy-independent-runners` | Reentrancy proof | Eight simultaneous independent `EpanetRunner` executions alternate between distinct hydraulic/quality requests and must match their sequential baselines without cross-project contamination |
| `aowis-server-epanet-conformance-negative-invalid-identifiers` | Upstream validation evidence | EPANET-invalid node, link, pattern, and curve identifiers are rejected through the adapter with structured entity diagnostics |
| `aowis-server-epanet-test-scenario-manifest` | Framework | Every C++ scenario is individually registered in CTest and vice versa |
| `aowis-server-epanet-upstream-test-inventory` | Framework | Every active vendored upstream source-level test has an explicit classification, and every wrapper candidate points to registered AOWIS conformance evidence |
| `aowis-server-epanet-model-field-conformance-audit` | Framework / audit | Every field in every audited hydraulic and water-quality Model struct is explicitly classified and every completed field points to registered evidence |

## Upstream EPANET inventory

`tests/upstream_test_inventory.txt` is the machine-readable classification and evidence map. Its CTest verifier scans the vendored EPANET test sources and fails when a test is added, removed, duplicated, or left unclassified. For every `wrapper-candidate`, it also verifies that every named evidence scenario exists in the registered AOWIS scenario manifest.

Each active upstream source-level test is assigned one of these classifications:

| Classification | Meaning |
|---|---|
| Wrapper candidate | The behavior maps to the AOWIS adapter contract and is represented by model-driven, native-differential, readback, export, or contract evidence. |
| Native-only | The case tests Toolkit CRUD, native handles/files, internal utilities, arbitrary native unit modes, or binary Output API behavior that the high-level adapter does not expose. |
| Not applicable | The specific upstream access path is outside the adapter contract and does not provide additional adapter-level evidence. |

The inventory verifier derives the active case set directly from the vendored EPANET sources, so this document does not duplicate source-level counts.

Two additional Boost cases are commented out upstream and are not active tests:

- `external/epanet/tests/test_net_builder.cpp`: `net_builder_I`
- `external/epanet/tests/outfile/test_output.cpp`: `AccessTest`

The source-level count is deliberately separate from upstream CTest's executable count. Upstream groups many Boost cases into a few executables, and it builds the C-string helper test without registering it with CTest.

## Evidence states

Every matrix row advances through these states:

| State | Required evidence |
|---|---|
| Inventory | The field or behavior has a row and an owning evidence area |
| Contract | A wrapper-only assertion verifies a non-default relationship or explicit diagnostic |
| Native compared | Native EPANET and the wrapper match at every applicable timestep with an independently normalized reference |
| Reference checked | At least one independent expected value or physical invariant also passes |
| Complete | Input mapping, result mapping, non-default behavior, negative behavior where relevant, and documentation are all present |

`0.0 == 0.0` is not coverage unless zero is the meaningful expected behavior and a paired scenario proves the field changes under a non-default input.

## Input coverage summary

| ID | Contract area | Hydraulic fields or behavior | Native EPANET path | Evidence areas | Evidence | State |
|---|---|---|---|---|---|---|
| I-OPTIONS-TIME | Duration and timing | duration, hydraulic/quality/report/pattern/rule timesteps, pattern/report start, start clock time | `EN_settimeparam` | timing options; controls | Every Model timing field has an independent native readback scenario; timer/time-of-day execution also verifies event timing | Complete |
| I-OPTIONS-HYDRAULIC | Solver configuration | headloss formula, demand model and limits, accuracy, trials, damping, status and convergence checks, demand multiplier/default pattern, emitter backflow, specific gravity, viscosity, unbalanced behavior | `EN_setoption`, `EN_setdemandmodel` | hydraulic options; input mapping | Every supported solver-option branch has an independent native readback test; all three headloss formulas are covered by the input-mapping and solver-option scenarios | Complete |
| I-OPTIONS-ENERGY | Energy configuration | global efficiency and price, global price pattern, demand charge | `EN_setoption` energy options | pump energy | Global-efficiency/global-price-pattern/demand-charge differential scenario with independent native cost accumulation | Complete |
| I-OPTIONS-REPORT | Report configuration | page, status, summary, messages, energy, node/link selection, typed fields, backend commands | `EN_setreport` and generated-INP/native reopen | report options; export fidelity | Report statistic, general flags, selected entities, every typed node/link field, precision/limits, and final backend overrides are verified through export/native parsing | Complete |
| I-PATTERN | Time patterns | IDs, multipliers, comments, default demand pattern, demand/head/speed/price references | pattern Toolkit APIs | hydraulic behavior; pump; export fidelity | Demand/reservoir/pump/energy references are differential-tested; factor values and comments are additionally verified through generated-INP/native reopen | Complete |
| I-CURVE-TANK | Tank volume curves | IDs, level points, volume points, comments, validation | curve Toolkit APIs | input mapping; export fidelity | Non-uniform tank volume behavior is differential-tested; curve type, points, and comment are verified through generated-INP/native reopen | Complete |
| I-CURVE-PUMP | Pump head and efficiency curves | one-point, three-point, multipoint head curves, efficiency curves, comments | curve Toolkit APIs and pump curve properties | pump; export fidelity | Pump curve behavior is differential-tested; exported curve type, points, and comments are verified through native reopen | Complete |
| I-CURVE-VALVE | Valve curves | GPV head-loss and PCV characteristic curves, comments | curve Toolkit APIs and valve curve properties | valve; export fidelity | Independent GPV/PCV behavior plus exported curve type, points, and comments are native-verified | Complete |
| I-JUNCTION | Junction inputs | elevation, demand categories, constant/pattern mode, emitter, enabled state, coordinates | node and demand Toolkit APIs | hydraulic behavior; input mapping; export fidelity; negative validation | Hydraulic inputs are native-compared; WGS84 coordinate export/native reopen and generic disabled-node pruning are verified explicitly | Complete |
| I-RESERVOIR | Reservoir inputs | head, constant/pattern mode, enabled state, coordinates | node Toolkit APIs | hydraulic behavior; input mapping; export fidelity; negative validation | Hydraulic head modes are native-compared; WGS84 coordinate export/native reopen and generic disabled-node pruning are verified explicitly | Complete |
| I-TANK | Tank inputs | elevation, levels, diameter, volume forms, curve, overflow, enabled state, coordinates | tank node Toolkit APIs | hydraulic behavior; input mapping; export fidelity; negative validation | All tank hydraulic geometry modes and overflow are native-compared; WGS84 coordinate export/native reopen and generic disabled-node pruning are verified explicitly | Complete |
| I-PIPE | Pipe inputs | endpoints, length, diameter, formula-specific roughness, minor loss, status/check valve, leakage, vertices, enabled state | link Toolkit APIs | hydraulic behavior; input mapping; export fidelity; negative validation | Hydraulic inputs are native-compared; ordered WGS84 vertices survive generated-INP/native reopen; generic disabled-link pruning is verified explicitly | Complete |
| I-PUMP | Pump inputs | endpoints, definition, curves, power, status, speed, speed pattern, efficiency, energy price/pattern, vertices, enabled state | pump link Toolkit APIs | pump; export fidelity; negative validation | All hydraulic pump modes are native-compared; common link metadata and ordered WGS84 vertex export/native reopen plus generic disabled-link pruning are verified explicitly | Complete |
| I-VALVE | Valve inputs | all seven types, endpoints, diameter, minor loss, status, setting, GPV/PCV curves, vertices, enabled state | valve link Toolkit APIs | hydraulic behavior; valve; export fidelity; negative validation | All seven valve types and curve behaviors are native-compared; common valve metadata and ordered WGS84 vertex export/native reopen plus generic disabled-link pruning are verified explicitly | Complete |
| I-CONTROL-SIMPLE | Simple controls | low/high level, timer, time of day, open/close/setting, enabled state | control Toolkit APIs | hydraulic behavior; controls/options/operations | Every simple-control type, action, and enabled-state branch has its own native readback/execution scenario | Complete |
| I-CONTROL-RULE | Structured rules | IF/AND/OR, object/variable/operator/value/status, THEN/ELSE, priority, enabled state | rule Toolkit APIs | hydraulic behavior; controls/options/operations | IF/AND/OR; THEN/ELSE; status/setting/ACTIVE actions; structured serialization/native readback; priority conflict resolution; disabled rules; every supported object-variable and comparison operator; explicit POWER rejection | Complete |
| I-PREPARATION | Snapshot preparation | disabled entity removal, control retention, invalid/disabled references, selected report entities | adapter preparation plus native counts | controls/options/operations; export fidelity; negative validation | Negative validation verifies generic disabled-node/link pruning, pruning from selected report entities, explicit rejection of enabled references to disabled entities, and deterministic broken-reference rejection before EPANET construction | Complete |
| I-METADATA | Export and geometry fidelity | titles, comments, tags, generic curves, coordinates, vertices; map-position fields are explicitly non-EPANET | metadata, coordinate, vertex, and INP APIs | export fidelity; field audit | Native-reopen scenarios verify titles, node/link/pattern/curve comments, node/link tags, generic curves, WGS84 coordinates, and ordered vertices. The field audit explicitly classifies optional GUI map-position semantics as non-EPANET. | Complete |

## Water-quality input mapping

These rows cover the water-quality configuration translated from the AOWIS model into native EPANET state.

| ID | Quality input family | Model fields / behavior | Native EPANET path | Evidence | State |
|---|---|---|---|---|---|
| QI-ANALYSIS | Analysis selection | none, chemical (`mg/L`), water age (`h`), source trace (`%`), trace node, mode-specific tolerance, relative diffusivity, quality timestep | `EN_setqualtype`, `EN_setoption`, `EN_settimeparam` | `conformance-quality-input-none`, `-chemical`, `-water-age`, `-source-trace` | Complete |
| QI-NODE | Initial quality and sources | typed initial quality; concentration, mass, flow-paced, setpoint sources; optional source pattern | `EN_INITQUAL`, `EN_SOURCETYPE`, `EN_SOURCEQUAL`, `EN_SOURCEPAT` | `conformance-quality-input-chemical` plus invalid-quality validation | Complete |
| QI-TANK | Tank quality configuration | all four mixing models, two-compartment fraction, tank bulk reaction override/default | `EN_MIXMODEL`, `EN_MIXFRACTION`, `EN_TANK_KBULK` | `conformance-quality-input-tank-mixing-models`, `-chemical`, `-reactions` | Complete |
| QI-REACTION | Reaction configuration | pipe/tank global effective coefficients, per-entity overrides, bulk/wall/tank orders, limiting concentration, roughness correlation for H-W/D-W/C-M | `EN_BULKORDER`, `EN_WALLORDER`, `EN_TANKORDER`, `EN_CONCENLIMIT`, `EN_KBULK`, `EN_KWALL`, `EN_TANK_KBULK` | `conformance-quality-input-chemical`, `-reactions`, invalid-quality validation | Complete |

## Water-quality execution and results

Quality execution uses its own timestep/result timeline. Saved hydraulics are executed through the native EPANET quality lifecycle and AOWIS results are compared against an independently stepped native project generated from the same AOWIS configuration.

| ID | Quality execution family | Behavior | Native EPANET path | Evidence | State |
|---|---|---|---|---|---|
| QE-LIFECYCLE | Quality lifecycle | disabled mode skips execution; chemical, age, and trace execute and close cleanly | `EN_openQ`, `EN_initQ`, `EN_runQ`, `EN_stepQ`, `EN_closeQ` | `conformance-quality-execution-none`, `-chemical`, `-water-age`, `-source-trace` | Complete |
| QE-TIMELINE | Independent quality timestep | quality samples occur at every quality step even when hydraulics are coarser | `EN_runQ`, `EN_stepQ` | `conformance-quality-execution-independent-timeline` | Complete |
| QE-RESULTS | Typed node/link results | junction, reservoir, tank, pipe, pump, valve quality; configured-source mass flow; mass balance | `EN_QUALITY`, `EN_SOURCEMASS`, `EN_LINKQUAL`, `EN_MASSBALANCE` | chemical/age/trace native differential scenarios | Complete |
| QE-CANCEL | Cancellation | preserves completed hydraulics and already-produced quality samples | stepwise quality lifecycle | `conformance-quality-execution-cancellation-partial` | Complete |
| QE-SOURCES | Runtime source families | concentration, mass booster, flow-paced booster, setpoint booster, and patterned dosing | source properties + quality stepping | `conformance-quality-runtime-source-*` | Complete |
| QE-MIXING | Runtime tank mixing | complete-mix, two-compartment, FIFO, and LIFO behavior | tank mixing model + quality stepping | `conformance-quality-runtime-tank-mixing-models` | Complete |
| QE-REACTIONS | Runtime reactions | pipe bulk/wall, tank bulk, per-entity overrides, limiting concentration, roughness correlation under H-W/D-W/C-M | reaction configuration + quality stepping | `conformance-quality-runtime-reactions` | Complete |
| QE-LONG-RUN | Long multi-step result contract | 12-hour 300-second quality timeline, per-step success status, finite positive mass balance, hydraulic-result isolation | `EN_runQ`, `EN_stepQ`, `EN_MASSBALANCE` | `conformance-quality-runtime-long-multistep-contract` | Complete |

## Water-quality deterministic stress coverage

The deterministic quality stress suite uses an independent fixed-seed generator that writes the native EPANET INP directly from the generated specification instead of exporting the AOWIS-built project. Quality values are written at full double precision, so the differential path does not include the INP re-serialization rounding that would otherwise accumulate over long water-quality runs.

| Scenario | Quality stress dimension |
|---|---|
| `conformance-quality-stress-chain-chemical-pattern` | 12-junction H-W chain, chemical transport, patterned concentration source, 300 s quality / 1800 s hydraulic steps |
| `conformance-quality-stress-branch-water-age` | 31-junction D-W branching tree, nonzero initial water age, 600 s quality / 3600 s hydraulic steps |
| `conformance-quality-stress-ring-source-trace` | 24-junction C-M ring/chord topology, source trace, 120 s quality / 1200 s hydraulic steps |
| `conformance-quality-stress-grid-reactions-dw` | 7x7 D-W grid, patterned chemical source, global reactions, roughness-correlated wall reactions, per-pipe overrides |
| `conformance-quality-stress-dual-source-reactions-hw` | 6x8 dual-source H-W mesh, independent source patterns and reaction-heavy execution |
| `conformance-quality-stress-grid-large-long-cm` | 10x10 / 100-junction C-M grid, 24-hour chemical timeline |
| `conformance-quality-stress-cancellation-positions` | Three distinct mid-quality cancellation positions with completed hydraulics preserved |

Every generated differential compares every returned node/link quality value, configured-source mass flow, quality timestamp, per-step status, and quality mass-balance ratio against raw Toolkit execution of the independent native INP. Byte-identical fixture regeneration is also asserted for every fixed seed.

## Result coverage summary

Water-quality members are intentionally omitted from this hydraulic matrix.

| ID | Result family | Fields requiring native comparison | Evidence areas | Evidence | State |
|---|---|---|---|---|
| R-TIMELINE | Timeline | validity, status, elapsed time, timestep sequence, cancellation, partial-result behavior | Net1; operations | Full native Net1 timeline plus independent cancellation-before-results and mid-run partial-result scenarios | Complete |
| R-EVENT | Next event | type, time until event, tank ID/UUID, control ID/UUID | Net1; operations | Full Net1 event sequence plus dedicated next-control-event timing and stable control ID/UUID scenarios | Complete |
| R-JUNCTION | Junction | ID/UUID, requested demand, delivered demand, deficit, total demand, emitter flow, leakage flow, head, pressure head, control membership | Net1; hydraulic behavior; input mapping | Net1 plus DDA/PDA, leakage, emitter, and multi-category demand scenarios compare every applicable junction result | Complete |
| R-RESERVOIR | Reservoir | ID/UUID, net demand, head, pressure head, control membership | Net1; input mapping | Net1 plus patterned-head reservoir scenario compare every reservoir result across events | Complete |
| R-TANK | Tank | ID/UUID, net demand, head, pressure head, level, volume, mixing-zone volume, control membership | Net1; hydraulic behavior; input mapping | Net1, overflow, and all AOWIS tank geometry input modes exercise tank result mapping | Complete |
| R-PIPE | Pipe | ID/UUID, flow, leakage, velocity, head loss, unit head loss, formula-specific roughness, friction factor, open state, control membership | Net1; hydraulic behavior; input mapping | Net1 plus leakage, non-default pipe inputs, check-valve/closed status, and H-W/D-W/C-M formula scenarios | Complete |
| R-PUMP | Pump | ID/UUID, flow, velocity, head gain, open state, operating state, speed, efficiency, power, control membership | pump | Every pump result field plus all four EPANET operating states, patterned speed, efficiency modes, and constant-power behavior | Complete |
| R-VALVE | Valve | ID/UUID, flow, velocity, head loss, open state, active state, setting, control membership | hydraulic behavior; valve | All seven EPANET/AOWIS valve types are independently native-compared, including regulating, sustaining, fixed-drop, flow-control, throttling, GPV-curve, and PCV-position-curve behavior | Complete |
| R-STATISTICS | Hydraulic statistics | iterations, relative error, maximum head error, maximum flow change, deficient nodes, demand reduction, leakage loss | hydraulic behavior; operations | Full native comparison plus dedicated statistics-population scenario; DDA/PDA/leakage cases cover the nonzero specialized fields | Complete |
| R-PUMP-ENERGY | Per-pump energy | pump ID/UUID, time online, average efficiency, average kW per flow unit, average power, peak power, average daily cost | pump | Independently accumulated native summaries with global and pump-specific patterned prices plus contracts | Complete |
| R-SYSTEM-ENERGY | System energy | energy cost per day, peak power, demand charge per day, total cost per day | pump | Independently accumulated patterned-cost and demand-charge differential scenarios plus contracts | Complete |
| R-FLOW-BALANCE | Run flow balance | total inflow/outflow, consumer demand, demand deficit, emitter flow, leakage flow, storage flow, balance ratio | hydraulic behavior; input mapping; operations | Full native comparison plus dedicated balance sanity scenario and nonzero demand-deficit/leakage coverage | Complete |
| R-DIAGNOSTICS | Diagnostics | stage, operation, entity, property, backend operation/code/message, warning/error validity | operations; negative validation; field audit | Operational warning/error branches and deterministic preparation-time diagnostics are covered; native warning backend provenance is asserted and the field audit classifies backend-local numeric indices separately from the stable high-level contract. | Complete |

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

Scenario-specific overrides must be explicit at the assertion and justified by native precision, numerical conditioning, or a documented reference-value precision. Exact enums, booleans, IDs, counts, and timestamps use exact comparisons.

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

A domain is complete only when every applicable matrix row is `Complete`, the model-field audit accounts for every audited field, completed fields point to registered evidence, excluded fields have explicit reasons, meaningful numeric results are exercised with non-default values, relevant upstream candidates have a documented disposition, and the default CTest suite passes.

The original upstream Boost suite and coverage reports are optional independent coverage layers: enable and run them for release/audit evidence when their external dependencies are available. They provide additional release evidence without changing the public adapter contract.

A conformance claim is supported when the default suite passes together with the proof checks and every included matrix row remains backed by registered evidence. Hydraulic and water-quality labels provide useful test grouping; they do not represent implementation stages.
