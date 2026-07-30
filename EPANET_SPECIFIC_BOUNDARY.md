# EPANET-Specific Boundary

The shared model no longer exposes EPANET-prefixed domain types. EPANET names are retained in this repository only where they describe the concrete backend adapter.

## Deliberately retained

- Repository, CMake target, include path, and server executable names containing `epanet`.
- `EpanetRunner` and `EpanetSimulationManager`, because selecting either explicitly selects the EPANET backend.
- `EpanetProject`, because it owns the native `EN_Project` handle.
- `EpanetNetworkBuilder`, `EpanetHydraulicSolver`, `EpanetResultReader`, `EpanetIndexRegistry`, and `EpanetReportCollector`, because they translate to or operate on the native EPANET API.
- `EpanetResolvers`, because it resolves generic tank input forms into the geometry required by EPANET.
- `EpanetResultRun`, because it combines the generic result timeline with EPANET-native report lines.
- `makeEpanetStatus`, `makeEpanetError`, and `makeEpanetSuccess`, because they are backend-adapter helpers that populate the generic status structure.
- Native `EN_*` constants and calls inside the adapter implementation.

## Removed from the public model boundary

- All former `EpanetNode*`, `EpanetLink*`, `EpanetCurve*`, `EpanetResult*`, and `EpanetStatus*` model types.
- Native EPANET function names represented as shared status enum values.
- `epanet_error_code` and `message_epanet` fields.
- EPANET-prefixed result and status printer classes.

## Adapter policy

Native API details must not be promoted back into the shared model. A native operation is mapped to a generic `HydraulicSimulationStatusOperation`; the exact `EN_*` call is stored only in `backend_operation`. Native numeric codes and messages are stored only in the generic `backend_*` diagnostics fields.

## Backend unit contract

The adapter initializes every native EPANET project with `EN_CMH` and explicitly selects `EN_METERS` pressure units. The adapter therefore passes implemented AOWIS values directly to EPANET in the units encoded by their field names:

- Flow, demand, leakage flow, and flow-change limits: `m3_per_h` (m³/h).
- Elevation, hydraulic head, pressure head, water level, pipe length, and tank diameter: `m`.
- Pipe diameter: `mm`.
- Darcy-Weisbach absolute roughness: `mm`.
- Hazen-Williams and Chezy-Manning roughness coefficients: dimensionless.
- Tank volume and tank-volume curve ordinates: `m3`.
- Velocity: `m_per_s`.
- Simulation time values: `s`.
- Pump power and demand charge basis: `kw`.
- Pump efficiency: `percent`.

No m³/h-to-L/s conversion is performed by this adapter. `HydraulicSolverOptions` does not expose selectable native flow or pressure units; conversion to presentation or interchange units belongs outside the hydraulic solver.

Pipe roughness is selected according to `headloss_formula`: `roughness_hw` for Hazen-Williams, `roughness_dw_mm` for Darcy-Weisbach, and `roughness_cm` for Chezy-Manning.

## Known model-boundary issues

The following model fields do not fully satisfy the canonical, self-describing unit contract:

- `emitter_coefficient_lps_per_m_exponent` is explicitly based on L/s, while the fixed backend flow unit is m³/h. It requires a quantity-aware conversion or, preferably, a canonical model representation coupled to its exponent.
- `leak_area_mm2_per_100m` uses EPANET's per-100-metre convention rather than the canonical AOWIS `mm2_per_m` representation. Pipe leakage is not currently written to EPANET.
- Generic valve and control `setting` values are context-dependent and do not carry a single unit in their names. They require discriminated quantity-specific representations before complete valve and control support can be unit-safe.

Emitter, FAVAD leakage, pump, valve, and control inputs are currently outside the implemented builder path. Networks containing these physical inputs are rejected explicitly instead of being simulated with omitted components. Pipe roughness results are exposed through `roughness_hw`, `roughness_dw_mm`, or `roughness_cm`, according to the selected headloss formula.
