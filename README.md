# AOWIS EPANET Backend

EPANET 2.3 simulation backend for the solver-neutral AOWIS hydraulic model.

The adapter translates `NetworkHydraulic` into a native EPANET project, executes one hydraulic analysis, optionally executes one or more water-quality analyses against the saved hydraulic solution, and maps native results and diagnostics back into AOWIS result types.

## Public API

- `EpanetRunner`: synchronous entry point for one complete EPANET run request.
- `EpanetSimulationManager`: asynchronous Qt queue with independent cooperative cancellation for each job.
- `EpanetRunRequest`: one hydraulic network plus an ordered list of requested water-quality analyses.
- `EpanetResultRun`: one hydraulic result timeline plus the results of the requested quality analyses.
- `EpanetResolvers`: conversion helpers for supported AOWIS input forms.

The implementation keeps native EPANET state behind adapter-internal builders, configurators, solvers, result readers, project wrappers, and index registries.

## Execution model

Each request contains exactly one hydraulic configuration. The selected headloss formula comes from `NetworkHydraulic::options_hydraulic.headloss_formula`.

Hydraulics are solved and saved once. The adapter then executes the ordered `quality_runs` list against that saved hydraulic solution. Chemical concentration, water age, and source trace use independent quality timelines because their timesteps do not have to coincide with hydraulic events.

Multiple headloss formulas are separate requests; they are not branches inside one `EpanetRunRequest`.

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

## Build

```bash
./compile_linux.sh
```

The EPANET dependency is the Git submodule at `external/epanet`:

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

Useful label groups include `contract`, `conformance`, `hydraulic`, `quality`, `negative`, `export`, `stress`, `proof`, and `upstream`.

`EPANET_CONFORMANCE.md` defines the conformance target, evidence model, test groups, acceptance rule, and supported hydraulic and water-quality coverage. `EPANET_CONFORMANCE_MATRIX.md` contains the detailed scenario and field-level evidence matrix.

## Reference documents

- `EPANET_ADAPTER_CONTRACT.md` — public adapter contract and model-to-EPANET translation rules.
- `EPANET_BACKEND_SEMANTICS.md` — backend-specific units, native constraints, result semantics, and enabled-state behavior.
- `EPANET_CONFORMANCE.md` — conformance scope and verification method.
- `EPANET_CONFORMANCE_MATRIX.md` — detailed coverage and evidence matrix.
