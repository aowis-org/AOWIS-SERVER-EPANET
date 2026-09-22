#!/usr/bin/env bash
set -euo pipefail

# Initialize only AOWIS' direct dependencies. In particular, do not recurse
# into external/epanet-msx/EPANET2.2: AOWIS links the MSX solver against its
# own external/epanet checkout and does not use the EPANET copy bundled by MSX.
git submodule update --init -- \
    external/epanet \
    external/GeographicLib \
    external/epanet-msx
