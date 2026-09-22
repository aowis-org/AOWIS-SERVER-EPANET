#!/usr/bin/env bash
set -euo pipefail

# Update only AOWIS' direct dependencies. Do not use --recursive here:
# external/epanet-msx contains an EPANET2.2 submodule that AOWIS does not use.
git submodule update --init --remote -- \
    external/epanet \
    external/GeographicLib \
    external/epanet-msx
