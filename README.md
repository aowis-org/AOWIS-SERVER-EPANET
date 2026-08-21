# AOWIS-SERVER-EPANET

EPANET simulation backend and REST server for AOWIS.

The shared AOWIS hydraulic model is solver-neutral. This repository is the EPANET adapter, so EPANET names remain only where they identify the backend implementation, its native API, or backend-specific output.

## Architecture

- `EpanetRunner`: synchronous EPANET adapter entry point. `run()` executes one hydraulic/quality configuration; `runBatch()` prepares one native project and executes the requested hydraulic and quality analyses sequentially.
- `EpanetSimulationManager`: asynchronous Qt queue for `EpanetBatchRequest`. Each submitted batch has its own cancellation token and produces one complete `EpanetResultBatch`.
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
EpanetRunner runner;
const EpanetResultRun result = runner.run(network);

if (!result.result_timeline.status.success)
    HydraulicSimulationStatusPrinter::print(result.result_timeline.status);
else
    HydraulicSimulationResultPrinter::print(result.result_timeline);
```


### Batch execution

`EpanetBatchRequest` separates the network definition from the requested solver analyses. The batch runner constructs the native EPANET network once, executes each requested headloss formula sequentially, and reuses that formula's saved hydraulic solution for its requested quality analyses.

`EpanetSimulationManager` is the asynchronous service boundary for this operation. `submit()` returns a simulation UUID, `cancel()` requests cancellation of one batch, `cancelAll()` requests cancellation of all queued/running batches, and `signalSimulationCompleted` returns the complete aggregate result regardless of whether the aggregate state is success, warning, error, or cancelled. Completed sub-results remain in the returned batch.

The standalone HTTP executable currently exposes only the `/status` liveness route. Solver execution is intentionally kept out of `server.cpp`; a future transport route should deserialize a batch request, submit it to `EpanetSimulationManager`, and serialize the final `EpanetResultBatch`.

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
