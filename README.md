# AOWIS EPANET Backend

EPANET 2.3 simulation backend for the solver-neutral AOWIS hydraulic model.

The adapter translates `NetworkHydraulic` into a native EPANET project, executes one hydraulic analysis, optionally executes one or more water-quality analyses against the saved hydraulic solution, and maps native results and diagnostics back into AOWIS result types.

## Public API

- `EpanetRunner`: synchronous entry point for one complete EPANET run request.
- `EpanetSimulationManager`: asynchronous Qt queue with independent cooperative cancellation for each job.
- `EpanetRunRequest`: the complete execution request: one hydraulic network plus an ordered list of requested water-quality analyses.
- `EpanetResultRun`: one hydraulic result timeline plus one child result for every requested quality analysis.
- `EpanetResultImport`: an INP import result containing the reconstructed `EpanetRunRequest`, structured diagnostics, and a completeness flag.
- `EpanetResolvers`: conversion helpers for supported AOWIS input forms.

The implementation keeps native EPANET state behind adapter-internal builders, configurators, solvers, result readers, project wrappers, and index registries.

## Execution model

Each request contains exactly one hydraulic configuration. The selected headloss formula comes from `NetworkHydraulic::options_hydraulic.headloss_formula`.

Hydraulics are solved and saved once. The adapter then executes the ordered `quality_runs` list against that saved hydraulic solution. Chemical concentration, water age, and source trace use independent quality timelines because their timesteps do not have to coincide with hydraulic events.

`NetworkHydraulic` contains the network and its persistent hydraulic/quality input data. The active water-quality analysis, trace origin, chemical name, tolerances, and diffusivity are execution settings carried only by `EpanetRunRequest::quality_runs`; they are not mutable network state.

Multiple headloss formulas are separate requests; they are not branches inside one `EpanetRunRequest`. `retrieveInp()` accepts the same request type because quality-analysis configuration is part of execution state, but an INP file can represent at most one active quality analysis, so INP export rejects requests containing more than one quality child.

```cpp
EpanetRunRequest request;
request.network = network;

WaterQualitySolverOptions water_age;
water_age.analysis = WaterQualityAnalysisType::WaterAge;
request.quality_runs.append(water_age);

EpanetRunner runner;
const EpanetResultRun result = runner.run(request);

if (!result.result_timeline.status.success)
    HydraulicSimulationStatusPrinter::print(result.result_timeline.status);
else
    HydraulicSimulationResultPrinter::print(result.result_timeline);
```

## Diagnostics

Native EPANET failures are represented through `HydraulicSimulationStatus` without leaking backend-specific concepts into the shared hydraulic model:

- `backend_name` is `EPANET`.
- `backend_error_code` contains the native numeric error code.
- `backend_operation` contains the native call, such as `EN_runH`.
- `message_backend` contains the EPANET error message.
- `operation`, `stage`, `property`, and `entity` remain solver-neutral AOWIS concepts.

See `EPANET_BACKEND_SEMANTICS.md` for backend-specific units, translation rules, constraints, and result semantics.

## HTTP server

The standalone server executable exposes the `/status` liveness route. Simulation execution is provided by the adapter API and `EpanetSimulationManager`; `server.cpp` does not expose a solver transport endpoint.

## INP import

`EpanetRunner::importInp()` opens an EPANET-formatted input file through the native Toolkit and reconstructs supported data into an `EpanetRunRequest`. The importer currently reconstructs project/global options, time patterns, typed curves, junctions, reservoirs, tanks, pipes, pumps, all EPANET valve families, simple controls, structured rules, and the active water-quality analysis configuration, including UUID-resolved pattern/curve/entity references, pump energy inputs, demand categories, emitters, tank geometry, pipe status/roughness/minor loss, EPANET 2.3 leakage inputs, control enabled state, rule premises/actions, priorities, canonical control/rule quantities, CHEMICAL/AGE/TRACE mode, quality tolerance, relative diffusivity, trace-node UUID, initial chemical/water-age values, all four chemical source types, canonical source strengths, source-pattern UUIDs, all four tank mixing models, global and entity reaction coefficients/orders, limiting concentration, and roughness/reaction correlation. After opening the source file, the importer asks EPANET itself to normalize the live project to `EN_CMH` flow units and `EN_METERS` pressure units, then reads canonical values directly from the Toolkit into the AOWIS fields. Import success and completeness are separate: a successful import can report `complete == false` together with structured warnings when source content cannot be represented. Water-quality import is now covered as a complete EPANET 2.3 solver-input layer: the dedicated fixtures plus upstream Net1 are native-compared after reconstruction, and valid quality imports are required to emit no deferred quality diagnostics. Map geometry is imported into AOWIS WGS84 fields. EPANET map coordinates declared `DEGREES` are interpreted as WGS84 longitude/latitude when all points fall in valid geographic ranges. `METERS` and `FEET` geometry is converted through GeographicLib and centered at WGS84 0°,0° while preserving its scale and layout; `NONE` or missing map-unit declarations use the documented synthetic convention of one EPANET map unit = one metre. Link vertices, labels, and backdrop geometry use the same transform. Nodes without source coordinates receive a deterministic schematic layout near the synthetic origin. Node/link comments and tags are imported through the native metadata getters. The `[REPORT]` section is reconstructed into typed AOWIS report options: page size, status, summary/messages/energy flags, node/link selections, field enablement/precision, and physical report thresholds are retained with canonical unit conversion. Backend-only report commands such as `FILE`, plus representable generic STATE/QUALITY/REACTION thresholds, are retained through `backend_commands`. A non-canonical `SETTING BELOW/ABOVE` filter is explicitly diagnosed as incomplete because EPANET applies one numeric threshold across link settings whose units differ by pump/valve type, so there is no lossless backend-neutral conversion after the project is normalized to AOWIS units. Documented EPANET `ug/L` chemical input is converted at the adapter boundary to canonical AOWIS `mg/L`; `QUALITY NONE` produces no quality child. Simple controls retain exact source OPEN/CLOSED versus numeric-setting intent. Reservoir-triggered level controls are represented directly, and GPV OPEN/CLOSED actions are recovered from the source `[CONTROLS]` statement because the public `EN_getcontrol()` getter does not expose GPV status separately. Valid EPANET 2.3 input semantics that are not representable by the current AOWIS model/adapter are treated as AOWIS coverage gaps to be implemented, not as permanently droppable import content.

## Build

```bash
./compile_linux.sh
```

The EPANET and GeographicLib dependencies are Git submodules at `external/epanet` and `external/GeographicLib`. When the adapter is embedded in AOWIS-SERVER-GUI, its already-configured GeographicLib target is reused instead of compiling a second copy:

```bash
git submodule update --init --recursive
```

## Tests and conformance

Configure, build, and run the default test suite with:

```bash
./compile_linux_tests.sh
```

CTest registers each scenario separately:

```bash
ctest --test-dir build-linux-tests -N
ctest --test-dir build-linux-tests -R <scenario-name> --verbose
```

Useful label groups include `contract`, `conformance`, `hydraulic`, `quality`, `negative`, `import`, `export`, `stress`, `proof`, and `upstream`.

`EPANET_CONFORMANCE.md` defines the conformance target, evidence model, test groups, acceptance rule, and supported hydraulic and water-quality coverage. `EPANET_CONFORMANCE_MATRIX.md` contains the detailed scenario and field-level evidence matrix.

## Reference documents

- `EPANET_ADAPTER_CONTRACT.md` — public adapter contract and model-to-EPANET translation rules.
- `EPANET_BACKEND_SEMANTICS.md` — backend-specific units, native constraints, result semantics, and enabled-state behavior.
- `EPANET_CONFORMANCE.md` — conformance scope and verification method.
- `EPANET_CONFORMANCE_MATRIX.md` — detailed coverage and evidence matrix.
