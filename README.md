# AOWIS-SERVER-EPANET

RESTful EPANET server and C++ integration for the AOWIS infrastructure.

## Architecture

The EPANET integration is split into focused components:

- `EpanetRunner`: synchronous public entry point returning the simulation timeline and EPANET report together.
- `EpanetSimulationManager`: asynchronous Qt interface for queued simulations.
- `EpanetProject`: RAII ownership of `EN_Project`.
- `EpanetNetworkBuilder`: construction of curves, reservoirs, junctions, tanks, and pipes.
- `EpanetIndexRegistry`: stores EPANET indices while the network is built.
- `EpanetHydraulicSolver`: owns the complete hydraulic-session lifecycle.
- `EpanetResultReader`: extracts timestep results without repeated ID lookups.
- `EpanetReportCollector`: owns callback state.
- Status helper functions: centralize repeated error construction.

```cpp
EpanetRunner runner;
const EpanetResultRun result = runner.run(network);

if (!result.result_timeline.status.success)
    EpanetStatusPrinter::print(result.result_timeline.status);
