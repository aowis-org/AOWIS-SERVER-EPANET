# EPANET Backend Semantics

The shared AOWIS hydraulic model is solver-neutral. EPANET-specific names and native Toolkit concepts are confined to the backend adapter, its public backend entry points, and backend diagnostics.

## EPANET-specific API and implementation names

The following names identify the concrete EPANET backend and are therefore intentionally backend-specific:

- Repository, CMake target, include path, and server executable names containing `epanet`.
- `EpanetRunner` and `EpanetSimulationManager`, because selecting either explicitly selects the EPANET backend.
- `EpanetRunRequest`, `EpanetResultRun`, `EpanetResultImport`, `EpanetQualityResult`, and `EpanetRunState`, because they describe EPANET backend execution/import orchestration around solver-neutral hydraulic and quality types.
- `EpanetProject`, because it owns the native `EN_Project` handle.
- `EpanetNetworkBuilder`, `EpanetHydraulicRunConfigurator`, `EpanetQualityRunConfigurator`, `EpanetHydraulicSolver`, `EpanetQualitySolver`, `EpanetResultReader`, `EpanetQualityResultReader`, `EpanetIndexRegistry`, and `EpanetReportCollector`, because they translate to or operate on the native EPANET API.
- `EpanetResolvers`, because it resolves generic AOWIS input forms into values required by EPANET.
- `makeEpanetStatus`, `makeEpanetError`, and `makeEpanetSuccess`, because they are backend helpers that populate the solver-neutral status structure.
- Native `EN_*` constants and calls inside the adapter implementation.

## Solver-neutral model boundary

Shared hydraulic entities, curves, controls, results, statuses, and diagnostics do not use EPANET-prefixed domain types. Native operation names are represented by generic `HydraulicSimulationStatusOperation` values, while the exact `EN_*` call is stored in `backend_operation`. Native numeric codes and messages are stored in the generic backend diagnostic fields.

EPANET-specific indices, handle types, function names, error constants, and Toolkit-only state remain implementation details of this adapter.

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
- Typed tank, pump, and valve curves use the canonical units encoded by their point fields.
- Hydraulic node coordinates and link vertices are exported as canonical WGS84 longitude/latitude in degrees. EPANET permits geographic coordinates, so generated `[COORDINATES]` and `[VERTICES]` use this WGS84 map space directly.
- Map labels use the same WGS84 coordinate representation as nodes and vertices. Enabled backdrop metadata stores WGS84 lower-left/upper-right bounds and longitude/latitude offsets in degrees; generated `[BACKDROP]` therefore declares `UNITS DEGREES`. GUI/map-space `x`/`y` position fields are not part of the hydraulic model.
- `HydraulicCurveGeneric` is an opaque EPANET-compatible preservation type. Its `x` and `y` coordinates are backend-defined numeric data, not AOWIS measurement quantities, and therefore do not carry canonical UCUM units. AOWIS hydraulic entities reference typed curves instead; generic curves are retained for complete backend data/export fidelity.
- PRV, PSV, and PBV settings: pressure head in `m`.
- FCV settings: `m3_per_h`.
- TCV settings: dimensionless loss coefficient.
- PCV settings: `percent` open.
- GPV head-loss curves are referenced by `head_loss_curve_uuid`; PCV valve-characteristic curves are referenced separately by `characteristic_curve_uuid`. Native GPV curve indices returned through `EN_SETTING` are backend identifiers and are not exposed as numeric AOWIS measurement results.
- Junction emitter relation: coefficient plus pressure exponent, representing `Q = C · p^n` with flow in `m3/h` and pressure head in `m`. The coefficient has no fixed standalone UCUM unit because its dimension depends on `n`.
- Water-quality model values are quantity-specific: chemical concentration uses `mg/L`, water age uses `h`, source trace uses `%`, and chemical source mass flow uses `mg/min`. Generic untyped initial/result `quality` and source `strength` scalars are not part of the Model boundary.
- Water-quality tolerance is quantity-specific for chemical concentration, water age, and source trace. AOWIS chemical concentration is canonical `mg/L`; the Model does not carry an arbitrary chemical-unit string.
- Bulk and wall reaction coefficients are stored together with their reaction order because coefficient dimensions depend on that order. The coefficient itself therefore has no fixed standalone UCUM suffix unless a specific order is assumed.

Simulation projects use canonical AOWIS units directly. INP import uses EPANET's own unit system as the inverse boundary: after the source file is opened, the live native project is normalized to `EN_CMH` and `EN_METERS`, and Toolkit getters therefore expose junction demands and emitters, reservoir/tank elevations and levels, tank dimensions/volumes, pipe length/diameter, Darcy-Weisbach roughness, and EPANET 2.3 pipe-leakage coefficients directly in the canonical units represented by AOWIS fields. Source flow/pressure selectors are not preserved as hydraulic model state. Hazen-Williams and Chezy-Manning roughness values remain formula-specific coefficients as represented by their dedicated model fields.

No m³/h-to-L/s conversion is performed by the simulation path. `HydraulicSolverOptions` does not expose selectable native flow or pressure units; conversion to presentation or interchange units belongs outside the hydraulic solver.

Pipe roughness is selected according to `headloss_formula`: `roughness_hazen_williams` for Hazen-Williams, `roughness_darcy_weisbach_mm` for Darcy-Weisbach, and `roughness_chezy_manning` for Chezy-Manning.

The Toolkit `EN_HEADLOSS` link result is the total hydraulic-head difference across the link in the configured head unit. For pipes, AOWIS stores this directly as `head_loss_m` and derives `head_loss_gradient_m_per_km` from the pipe length. This is distinct from EPANET's formatted pipe report, which presents pipe head loss as a per-length gradient.

Typed report thresholds use AOWIS canonical units in the Model (`m`, `m3_per_h`, `mm`, `m_per_s`, `m_per_km`, or the dimensionless friction factor). Report fields whose numeric meaning depends on quality mode or link type do not expose typed `BELOW`/`ABOVE` thresholds; backend-specific report commands remain available through `backend_commands` when such EPANET-native configuration is required.

## Model-boundary conventions and backend constraints

Leakage uses the EPANET-compatible AOWIS engineering convention `leak_area_mm2_per_100m` for distributed leak area and `leak_area_expansion_per_pressure_head_mm2_per_m` for pressure-head-dependent area expansion. The adapter writes these canonical model values directly to `EN_LEAK_AREA` and `EN_LEAK_EXPAN`.

Simple-control and rule values use quantity-specific Model fields. Pump settings are speed ratios; valve settings use the same pressure-head, flow, loss-coefficient, or position quantities as the target valve type. Level controls distinguish tank water level from junction pressure head, and rule premises distinguish demand, head, level, pressure, flow, power, and time quantities explicitly. GPV curve selection is not represented as a numeric control setting.

Pumps and all seven EPANET 2.3 valve types are implemented, including their curves, patterns, energy inputs, geometry, hydraulic results, states, and pump energy summaries. EPANET has no pump-specific numeric constant-efficiency field, so the adapter represents that model option with a private one-point efficiency curve. EPANET also treats a pump-specific energy price of zero as inheritance from the global price; the adapter rejects that unrepresentable override explicitly.

FAVAD leakage and control inputs are implemented in the builder path. Pipe fixed-area and pressure-dependent leakage parameters are validated and written to EPANET; pipe and junction leakage flows, the leakage-loss statistic, and leakage in the run flow balance are exposed in the simulation results. Junction emitters carry their coefficient and pressure exponent together. EPANET supports one network-wide emitter exponent, so enabled non-zero emitters must use the same exponent; the adapter writes that exponent to `EN_EMITEXPON` and each coefficient to `EN_EMITTER` in the canonical m³/h and meter-head backend units. Pipe roughness results are exposed through `roughness_hazen_williams`, `roughness_darcy_weisbach_mm`, or `roughness_chezy_manning`, according to the selected headloss formula.

The bundled EPANET 2.3 headers expose `EN_R_POWER`, but its rule parser rejects pump `POWER` premises and its rule evaluator does not implement them. The adapter therefore rejects structured pump-power premises explicitly instead of forwarding a rule that cannot execute. Other implemented simple and rule-based controls are passed to EPANET normally.

## Water-quality input translation

The adapter maps the typed AOWIS water-quality configuration into the live EPANET project for the requested quality analysis. The adapter configures `None`, chemical, water-age, and source-trace analysis modes; canonical chemical units are `mg/L`. Initial node quality is selected from the quantity-specific Model field for the active analysis mode. Chemical concentration, mass-booster, flow-paced, and setpoint sources map to the corresponding EPANET source type, including optional source patterns. Tank mixing model/fraction, quality tolerance, relative diffusivity, quality timestep, reaction orders, limiting concentration, and pipe/tank reaction coefficients are also mapped. Source-trace node UUIDs and source-pattern UUIDs are resolved only after the referenced EPANET nodes/patterns exist.

EPANET exposes reaction orders through the Toolkit but does not expose Toolkit setters for the INP-level `GLOBAL BULK`, `GLOBAL WALL`, or `ROUGHNESS CORRELATION` directives. AOWIS therefore writes the effective coefficient to every pipe/tank through `EN_KBULK`, `EN_KWALL`, and `EN_TANK_KBULK`. Per-entity overrides take precedence. When roughness correlation is enabled, the adapter applies EPANET's own formula for the selected hydraulic head-loss model (Hazen-Williams, Darcy-Weisbach, or Chezy-Manning). This preserves the live simulation state even though a subsequently generated INP may normalize a global/roughness rule into explicit per-entity reaction coefficients.

EPANET reaction orders are network-wide. AOWIS stores each reaction coefficient together with its semantic order, and enabled per-pipe/per-tank overrides must use the corresponding network-wide order. EPANET wall reaction order is restricted to 0 or 1. Configured chemical quality sources may remain present while another quality analysis is selected; they are applied only during chemical runs. Source-trace analysis requires an enabled trace-node reference.

## Water-quality result boundary

Water-quality results are intentionally separate from `HydraulicSimulationResultTimeline`. Hydraulic events and water-quality steps do not have to occur at the same times, so quality values are not embedded in hydraulic timestep results. `WaterQualitySimulationResultTimeline` carries its own analysis mode, status, validity, diagnostics, simulation start time, and timestep results. Its initial validity is `NotRun`, which distinguishes a quality analysis that has not executed from one that executed and failed.

`EpanetQualitySolver` executes the saved-hydraulics quality lifecycle with `EN_openQ`, `EN_initQ`, `EN_runQ`, `EN_stepQ`, and `EN_closeQ`. `EN_stepQ` is used deliberately so the AOWIS quality timeline contains every configured quality timestep rather than only hydraulic-event samples. `EpanetQualityResultReader` reads node quality, source mass flow for configured sources, link quality, and quality mass balance at each returned timestep. Chemical, water-age, and source-trace analyses populate their quantity-specific result fields; analysis `None` skips the quality lifecycle and leaves the timeline `NotRun`.

Hydraulic and quality validity are finalized independently. A failure or cancellation after hydraulics have completed does not downgrade an already-valid hydraulic timeline. Cancellation during quality stepping preserves completed quality samples and marks only the quality timeline partial. Quality warnings and errors are kept on the quality timeline rather than being used to invalidate hydraulic results.

## Enabled-state preparation

Before an EPANET project is created, the adapter builds a simulation snapshot containing only enabled nodes and links. Disabled nodes and links remain in the editable AOWIS model but are omitted from the backend project and from simulation results. Simple controls and rules remain in the snapshot regardless of their enabled state so EPANET can retain their membership while `EN_setcontrolenabled` and `EN_setruleenabled` prevent disabled controls from executing. A control or rule that references an omitted, disabled, or unknown entity is rejected explicitly. An enabled link that references a disabled or unknown node is also rejected explicitly. Selected report lists are reduced by disabled entities while unresolved non-disabled UUIDs still produce an error.
