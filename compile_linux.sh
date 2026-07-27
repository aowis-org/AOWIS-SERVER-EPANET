#!/usr/bin/env bash
set -euo pipefail
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build build-linux --parallel

./build-linux/src/server/aowis-server-epanet

