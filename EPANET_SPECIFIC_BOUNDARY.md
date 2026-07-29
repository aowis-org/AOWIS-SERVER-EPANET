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

The adapter currently uses L/s and meters internally. Canonical AOWIS flow values are converted between m3/h and L/s at the backend boundary. The selected model flow and pressure display units are not used as storage units inside this adapter.
