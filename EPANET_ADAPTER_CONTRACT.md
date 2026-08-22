# EPANET Adapter Contract

This document defines how the solver-neutral AOWIS hydraulic model is translated to EPANET 2.3 and how native results are represented by the public adapter API.

## API ownership

Shared hydraulic entities, options, results, statuses, and diagnostics remain backend-neutral. EPANET-specific names are used only for the backend entry points and implementation types that directly own or operate on native EPANET state.

Public backend types:

- `EpanetRunner` and `EpanetSimulationManager` select the EPANET backend.
- `EpanetRunRequest`, `EpanetResultRun`, `EpanetQualityResult`, and `EpanetRunState` describe EPANET run orchestration around solver-neutral result timelines.
- `EpanetResolvers` converts supported AOWIS input forms into values required by EPANET.

Internal types such as `EpanetProject`, builders, configurators, solvers, result readers, index registries, and report collectors may expose native concepts because they are confined to the adapter implementation. Native `EN_*` calls, constants, indices, error codes, and messages do not become shared model concepts.

## Native project configuration

Every project uses `EN_CMH` flow units and `EN_METERS` pressure units. Values are passed in the canonical units encoded by the AOWIS field names:

| Quantity | Adapter unit |
|---|---|
| Flow, demand, leakage, flow-change limit | m³/h |
| Elevation, head, pressure head, water level, length | m |
| Pipe diameter and Darcy-Weisbach roughness | mm |
| Tank volume | m³ |
| Velocity | m/s |
| Time | s |
| Pump power | kW |
| Energy intensity | kWh/m³ |
| Efficiency and source trace | % |
| Chemical concentration | mg/L |
| Chemical source mass flow | mg/min |
| Water age | h |
| Geographic coordinates | WGS84 degrees |

Hazen-Williams and Chezy-Manning roughness values are dimensionless. Curve point units are determined by the typed curve model. `HydraulicCurveGeneric` preserves backend-defined numeric curve data and therefore has no implied AOWIS measurement unit.

No presentation-unit conversion is performed by the adapter. Currency values use `PumpEnergyOptions::currency_iso4217`; EPANET receives numeric prices and charges but performs no currency conversion.

## Hydraulic translation

The selected headloss formula determines which pipe roughness field is written:

- Hazen-Williams: `roughness_hazen_williams`.
- Darcy-Weisbach: `roughness_darcy_weisbach_mm`.
- Chezy-Manning: `roughness_chezy_manning`.

The native `EN_HEADLOSS` link result is the total hydraulic-head difference across a link. Pipe results store that value as `head_loss_m` and derive `head_loss_gradient_m_per_km` from pipe length.

Junction emitter coefficients and their pressure exponents represent `Q = C · p^n` in m³/h and metres of pressure head. EPANET supports one emitter exponent for the network, so all enabled non-zero emitters must use the same exponent.

FAVAD leakage uses `leak_area_mm2_per_100m` and `leak_area_expansion_per_pressure_head_mm2_per_m`. These values map directly to `EN_LEAK_AREA` and `EN_LEAK_EXPAN`. The adapter returns pipe leakage, junction-attributed leakage, leakage loss, and the leakage contribution to flow balance.

Pumps support constant power, typed head curves, speed and speed patterns, efficiency configuration, and energy pricing. EPANET has no pump-specific constant-efficiency scalar, so the adapter represents that input with a private one-point efficiency curve. A pump-specific price of zero cannot be distinguished from inheritance in EPANET and is rejected as an unrepresentable override.

The adapter supports PRV, PSV, PBV, FCV, TCV, GPV, and PCV valves. Their settings retain their physical meaning:

- PRV, PSV, and PBV: pressure head in metres.
- FCV: flow in m³/h.
- TCV: dimensionless loss coefficient.
- PCV: percentage open.
- GPV: typed headloss-curve reference.

Simple-control and rule values use quantity-specific model fields. The bundled EPANET 2.3 rule parser exposes `EN_R_POWER` but cannot execute pump-power premises, so the adapter rejects that premise explicitly. Other supported controls and rules are translated to the native Toolkit.

## Water-quality translation

Each requested quality analysis is configured independently after hydraulics have completed and been saved. Supported analyses are chemical concentration, water age, and source trace.

The adapter maps:

- quantity-specific initial node values;
- concentration, mass-booster, flow-paced, and setpoint sources;
- optional source patterns;
- trace-node selection;
- complete-mix, two-compartment, FIFO, and LIFO tank mixing;
- mode-specific tolerance, relative diffusivity, and quality timestep;
- network reaction orders and coefficients;
- per-pipe and per-tank reaction overrides;
- limiting concentration and roughness correlation.

Chemical sources are applied only during chemical analyses. Source-trace analysis requires an enabled trace node.

EPANET reaction orders are network-wide. Enabled entity overrides must therefore use the applicable network order, and wall reaction order must satisfy EPANET's supported values. Because the Toolkit does not provide setters for every INP-level global reaction directive, the adapter writes effective reaction coefficients to individual pipes and tanks. Per-entity overrides take precedence. Roughness-correlated coefficients use EPANET's formula for the selected headloss model.

## Execution and result timelines

One `EpanetRunRequest` performs exactly one hydraulic analysis. Its ordered `quality_runs` list may be empty or contain any number of quality analyses. The hydraulic solution is saved once and reused by every quality child.

Water-quality analysis selection is execution state. `NetworkHydraulic` stores the network's quality inputs and reaction data, while each `WaterQualitySolverOptions` entry in `EpanetRunRequest::quality_runs` supplies the active analysis mode, chemical name, trace node, tolerance, and diffusivity for one child run. The adapter does not mutate the prepared network between quality children.

Hydraulic and quality results use separate timelines because their timesteps and event boundaries can differ. Quality execution uses `EN_stepQ`, so every configured quality timestep is represented rather than only hydraulic event times.

Each quality timeline contains its analysis mode, status, validity, diagnostics, simulation start, and timestep results. Analysis `None` produces a `NotRun` timeline. A failed or cancelled quality child does not invalidate completed hydraulics. Cancellation preserves samples already returned by the active solver lifecycle.

## Enabled entities and references

The prepared simulation snapshot contains enabled nodes and links only. Disabled entities remain in the editable AOWIS model but are absent from the native project and simulation results.

Controls and rules remain in the snapshot so their membership is preserved; their enabled flags determine whether EPANET may execute them. References from enabled entities, controls, or rules to missing or disabled entities are rejected. Report selections silently discard disabled entities but reject unresolved enabled references.

## Geometry and export

Node coordinates, link vertices, map labels, and backdrop bounds use the model's WGS84 coordinate fields. Exported `[COORDINATES]`, `[VERTICES]`, and `[BACKDROP]` sections therefore use geographic degrees.

INP export preserves supported project titles, comments, tags, patterns, curves, report options, coordinates, vertices, labels, and backdrop metadata. Generic curves are retained for backend fidelity even when no typed AOWIS entity references them. `retrieveInp()` accepts an `EpanetRunRequest`; because one INP file can encode only one active quality analysis, export accepts zero or one quality child and rejects requests containing more than one.

## Diagnostics

Native failures are represented by `HydraulicSimulationStatus` and `HydraulicSimulationDiagnostic`:

- `backend_name` is `EPANET`.
- `backend_error_code` stores the native numeric error code.
- `backend_operation` stores the native call, such as `EN_runH`.
- `message_backend` stores the native message.
- `stage`, `operation`, `property`, `entity`, `message`, and `details` remain backend-neutral AOWIS fields.

Validation collects independent input problems where continued inspection is safe, allowing callers to highlight multiple invalid entities from one request.

## Public scope limits

The adapter's public contract is model-to-EPANET simulation and INP export. It does not expose:

- arbitrary INP import into `NetworkHydraulic`;
- mutation of an already-open native project through Toolkit CRUD calls;
- direct project-handle access;
- native pattern-file or hydraulic-file management;
- the binary Output API as a public interface;
- `EN_runproject` as an alternate execution path.

These native APIs remain available inside the bundled EPANET dependency but are not part of `EpanetRunner`.
