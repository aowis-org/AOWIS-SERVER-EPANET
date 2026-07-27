# EPANET dependency

This directory is the repository's EPANET Git submodule.

- Upstream: `OpenWaterAnalytics/EPANET`
- Gitlink revision used by the source repository at the time of this refactor: `e1509b2`

The ZIP does not vendor the contents of that external repository. Use this refactor on top of a clone of `AOWIS-SERVER-EPANET`, or place a compatible EPANET checkout in this directory before configuring CMake.

From a Git clone:

```bash
git submodule update --init --recursive
```
