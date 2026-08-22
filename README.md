# AOWIS-SERVER-EPANET

EPANET simulation backend and REST server for AOWIS.

The shared AOWIS hydraulic model is solver-neutral. This repository is the EPANET adapter, so EPANET names remain only where they identify the backend implementation, its native API, or backend-specific output.

## Architecture

- `EpanetRunner`: synchronous EPANET adapter entry point. `run(const EpanetRunRequest &)` executes one hydraulic configuration from `NetworkHydraulic`, followed by zero or more requested quality analyses that reuse the saved hydraulic solution.
- `EpanetSimulationManager`: asynchronous Qt queue for `EpanetRunRequest` jobs. Each job owns independent cooperative-cancellation state and completes with one `EpanetResultRun`.
- `EpanetProject`: RAII ownership of the native `EN_Project` handle.
- `EpanetNetworkBuilder`: converts generic hydraulic model entities into the static EPANET network topology and entity data.
- `EpanetHydraulicRunConfigurator`: applies the selected headloss formula and its formula-specific pipe roughness values.
- `EpanetQualityRunConfigurator`: applies the selected water-quality mode, initial values, sources, mixing, and reactions.
- `EpanetIndexRegistry`: stores native EPANET indices while the network is built.
- `EpanetHydraulicSolver`: owns the native hydraulic-session lifecycle and report configuration.
- `EpanetResultReader`: converts native EPANET result values into generic hydraulic result structures.
- `EpanetReportCollector`: owns the EPANET report callback state.
- `HydraulicSimulationResultPrinter` and `HydraulicSimulationStatusPrinter`: print the solver-neutral model result and status types.

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


### Multi-quality execution contract

`EpanetRunRequest` contains one `NetworkHydraulic` and an ordered list of `WaterQualitySolverOptions`. The network's `options_hydraulic.headloss_formula` is the single hydraulic formula for that run. `EpanetResultRun` contains one hydraulic result timeline plus zero or more `EpanetQualityResult` children. Multiple source-trace runs remain independently identifiable through their quality options.

The execution contract has exactly one hydraulic run. Multiple headloss formulas are not represented as branches of one request; callers choose one formula through `NetworkHydraulic::options_hydraulic.headloss_formula`. The executor saves that hydraulic solution once and reuses it for each requested quality analysis.

The standalone HTTP executable currently exposes only the `/status` liveness route. Solver execution is intentionally kept out of `server.cpp`; a future transport route should submit `EpanetRunRequest` jobs through `EpanetSimulationManager` and consume `EpanetResultRun` results.

## Backend diagnostics

The adapter maps native errors into `HydraulicSimulationStatus`:

- `backend_name` is `EPANET`.
- `backend_error_code` contains the native numeric error code.
- `backend_operation` contains the exact native call, such as `EN_runH`.
- `message_backend` contains the native EPANET error message.
- `operation`, `stage`, `property`, and `entity` remain generic AOWIS concepts.

See `EPANET_SPECIFIC_BOUNDARY.md` for the names deliberately retained as EPANET-specific.

## Hydraulic tests

Configure, build, and run the adapter tests with:

```bash
./compile_linux_tests.sh
```

The adapter test executable uses a named scenario registry, and CTest registers each scenario separately. Run `ctest --test-dir build-linux-tests -N` to list them or use `ctest --test-dir build-linux-tests -R <name> --verbose` to run one scenario.

The `conformance-net1` scenario opens the vendored upstream Net1 INP directly with native EPANET, independently constructs the same hydraulic network as `NetworkHydraulic`, runs it through `EpanetRunner`, and compares every hydraulic event and applicable result field.

See `EPANET_HYDRAULIC_CONFORMANCE.md` for the coverage matrix, upstream-test classification, scope, tolerances, and the evidence required before claiming complete hydraulic conformance.
