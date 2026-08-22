# INP Import Fixtures

This directory contains INP files used as AOWIS importer regression fixtures. Keep importer-specific sample networks here rather than modifying or mixing them with the vendored upstream EPANET example networks under `external/epanet`.

Fixtures should remain human-readable and should exercise source-file semantics that the importer must reconstruct into canonical AOWIS model values.

Current purpose-built fixtures:

- `import_global_options_us.inp` exercises project/global option import and native EPANET unit normalization.
- `import_core_topology_us.inp` exercises core junction/reservoir/tank/pipe import, endpoint reconstruction, emitter normalization, Darcy-Weisbach roughness, pipe state, and EPANET 2.3 leakage conversion.
