# AOWIS-SERVER-EPANET

EPANET simulation backend and REST server for AOWIS.

The shared AOWIS hydraulic model is solver-neutral. This repository is the EPANET adapter, so EPANET names remain only where they identify the backend implementation, its native API, or backend-specific output.

## Architecture

- `EpanetRunner`: synchronous EPANET adapter entry point. It accepts `NetworkHydraulic` and returns a generic `HydraulicSimulationResultTimeline` together with the native EPANET report.
- `EpanetSimulationManager`: asynchronous Qt queue around `EpanetRunner`.
- `EpanetProject`: RAII ownership and configuration of the native `EN_Project` handle.
- `EpanetNetworkBuilder`: converts generic hydraulic model entities into EPANET nodes, links, and curves.
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
