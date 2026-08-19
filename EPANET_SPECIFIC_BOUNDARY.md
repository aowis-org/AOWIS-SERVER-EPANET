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
- Pump energy intensity: AOWIS stores `kW.h/m3`; the wrapper computes it from canonical `kW` and `m3/h` values.
- Energy prices and costs use the single `PumpEnergyOptions::currency_iso4217` billing currency. EPANET receives only the numeric price/charge values and performs no currency conversion; exchange-rate handling belongs to the AOWIS controller.
- Pump efficiency: `percent`.
- Pump and valve curves use the canonical units encoded by their point fields.
- PRV, PSV, and PBV settings: pressure head in `m`.
- FCV settings: `m3_per_h`.
- TCV settings: dimensionless loss coefficient.
- PCV settings: `percent` open.
- GPV head-loss curves are referenced by `head_loss_curve_uuid`; PCV valve-characteristic curves are referenced separately by `characteristic_curve_uuid`. Native GPV curve indices returned through `EN_SETTING` are backend identifiers and are not exposed as numeric AOWIS measurement results.
- Junction emitter coefficient: `m3_per_h_per_m_exponent`.

No m³/h-to-L/s conversion is performed by this adapter. `HydraulicSolverOptions` does not expose selectable native flow or pressure units; conversion to presentation or interchange units belongs outside the hydraulic solver.

Pipe roughness is selected according to `headloss_formula`: `roughness_hazen_williams` for Hazen-Williams, `roughness_darcy_weisbach_mm` for Darcy-Weisbach, and `roughness_chezy_manning` for Chezy-Manning.

The Toolkit `EN_HEADLOSS` link result is the total hydraulic-head difference across the link in the configured head unit. For pipes, AOWIS stores this directly as `head_loss_m` and derives `head_loss_gradient_m_per_km` from the pipe length. This is distinct from EPANET's formatted pipe report, which presents pipe head loss as a per-length gradient.

## Model-boundary conventions and remaining issues

Leakage uses the EPANET-compatible AOWIS engineering convention `leak_area_mm2_per_100m` for distributed leak area and `leak_area_expansion_per_pressure_head_mm2_per_m` for pressure-head-dependent area expansion. The adapter writes these canonical model values directly to `EN_LEAK_AREA` and `EN_LEAK_EXPAN`.

Generic control `setting` values remain context-dependent and do not carry a single unit in their names. They require discriminated quantity-specific representations before complete control support can be unit-safe.

Pumps and all seven EPANET 2.3 valve types are implemented, including their curves, patterns, energy inputs, geometry, hydraulic results, states, and pump energy summaries. EPANET has no pump-specific numeric constant-efficiency field, so the adapter represents that model option with a private one-point efficiency curve. EPANET also treats a pump-specific energy price of zero as inheritance from the global price; the adapter rejects that unrepresentable override explicitly.

FAVAD leakage and control inputs are implemented in the builder path. Pipe fixed-area and pressure-dependent leakage parameters are validated and written to EPANET; pipe and junction leakage flows, the leakage-loss statistic, and leakage in the run flow balance are exposed in the simulation results. Junction emitters are written directly to EPANET using the canonical m³/h and meter-head backend units. Pipe roughness results are exposed through `roughness_hazen_williams`, `roughness_darcy_weisbach_mm`, or `roughness_chezy_manning`, according to the selected headloss formula.

The bundled EPANET 2.3 headers expose `EN_R_POWER`, but its rule parser rejects pump `POWER` premises and its rule evaluator does not implement them. The adapter therefore rejects structured pump-power premises explicitly instead of forwarding a rule that cannot execute. Other implemented simple and rule-based controls are passed to EPANET normally.

## Enabled-state preparation

Before an EPANET project is created, the adapter builds a simulation snapshot containing only enabled nodes and links. Disabled nodes and links remain in the editable AOWIS model but are omitted from the backend project and from simulation results. Simple controls and rules remain in the snapshot regardless of their enabled state so EPANET can retain their membership while `EN_setcontrolenabled` and `EN_setruleenabled` prevent disabled controls from executing. A control or rule that references an omitted, disabled, or unknown entity is rejected explicitly. An enabled link that references a disabled or unknown node is also rejected explicitly. Selected report lists are reduced by disabled entities while unresolved non-disabled UUIDs still produce an error.
