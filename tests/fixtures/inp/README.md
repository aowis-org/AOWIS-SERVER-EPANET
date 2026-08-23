# INP Import Fixtures

This directory contains INP files used as AOWIS importer regression fixtures. Keep importer-specific sample networks here rather than modifying or mixing them with the vendored upstream EPANET example networks under `external/epanet`.

Fixtures should remain human-readable and should exercise source-file semantics that the importer must reconstruct into canonical AOWIS model values.

Current purpose-built fixtures:

- `import_global_options_us.inp` exercises project/global option import and native EPANET unit normalization.
- `import_core_topology_us.inp` exercises core junction/reservoir/tank/pipe import, endpoint reconstruction, emitter normalization, Darcy-Weisbach roughness, pipe state, and EPANET 2.3 leakage conversion.
- `import_patterns_curves_pumps_us.inp` exercises typed curve normalization, time-pattern references, tank-volume curves, curve- and constant-power pumps, speed patterns, efficiency curves, and pump/global energy inputs.
- `import_valves_us.inp` exercises PRV, PSV, PBV, FCV, TCV, GPV, and PCV import with canonical settings, explicit statuses, and GPV/PCV curve references.
- `import_controls_rules_us.inp` exercises all four simple-control trigger types, junction/tank/reservoir trigger nodes, disabled controls, exact OPEN/CLOSED versus numeric-setting intent, GPV OPEN/CLOSED controls, structured IF/AND/OR rules, THEN/ELSE actions, priorities, and canonical unit normalization for control/rule thresholds and settings.

- `import_quality_chemical_ug_l.inp` exercises CHEMICAL mode, documented `ug/L` to canonical `mg/L` conversion, tolerance/diffusivity, initial node quality, and native quality equivalence without sources or reactions.
- `import_quality_sources_ug_l.inp` exercises CONCEN, MASS, FLOWPACED, and SETPOINT sources, source-pattern UUID reconstruction, `ug/L` to canonical `mg/L` concentration conversion, `ug/min` to canonical `mg/min` mass-injection conversion, and native source behavior.
- `import_quality_mixing_reactions_ug_l.inp` exercises all four tank mixing models, two-compartment fraction, positive/zero/negative reaction orders, global bulk/wall reactions, independent pipe bulk/wall overrides, tank override ranges, limiting concentration, roughness correlation, reaction-coefficient canonicalization from `ug/L` to `mg/L`, and native quality equivalence.
- `import_quality_age.inp` exercises AGE mode, initial water age, tolerance, quality timestep, and native quality equivalence.
- `import_quality_trace.inp` exercises TRACE mode, trace-node UUID resolution, percent tolerance, quality timestep, and native quality equivalence.
- `import_quality_wall_reaction_us.inp`: US-customary CHEMICAL fixture proving zero-order wall-reaction area-basis conversion to canonical square-metre semantics.
