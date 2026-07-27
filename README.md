# AOWIS-SERVER-EPANET

RESTful EPANET server and C++ wrapper for the AOWIS infrastructure.

## Wrapper refactor

The former large `EpanetWrapper` implementation is now a compatibility facade. The actual work is split into focused components:

- `EpanetRunner`: synchronous public entry point returning timeline and report together.
- `EpanetProject`: RAII ownership of `EN_Project`.
- `EpanetNetworkBuilder`: construction of curves, reservoirs, junctions, tanks, and pipes.
- `EpanetIndexRegistry`: stores EPANET indices while the network is built.
- `EpanetHydraulicSolver`: owns the complete hydraulic-session lifecycle.
- `EpanetResultReader`: extracts timestep results without repeated ID lookups.
- `EpanetReportCollector`: owns callback state.
- Status helper functions: centralize repeated error construction.

Existing users can continue to use `EpanetWrapper` and `EpanetSimulationManager`. New synchronous code should prefer `EpanetRunner`.

```cpp
EpanetRunner runner;
const EpanetRunResult result = runner.run(network);

if (!result.timeline.status.success)
    SimulationStatusPrinter::print(result.timeline.status);
```

## Build

The archive contains the complete AOWIS refactor, but it intentionally does not vendor the external EPANET Git submodule. Apply the archive over a normal repository clone, or populate `external/epanet` with the compatible EPANET source before configuring CMake.

From a Git clone:

```bash
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

When the model repository is already present locally:

```bash
cmake -S . -B build -DAOWIS_SERVER_MODEL_SOURCE_DIR=/path/to/AOWIS-SERVER-MODEL
```

The current builder intentionally preserves the feature coverage of the wrapper before this architectural refactor: tank volume curves, reservoirs, junctions and demand categories, tanks, and pipes. Pumps, valves, patterns, controls, quality, and energy should be added as focused follow-up functionality rather than mixed into this structural change.
