#!/usr/bin/env bash
set -euo pipefail

cmake -S . -B build-linux-tests -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DAOWIS_SERVER_EPANET_BUILD_SERVER=OFF \
    -DAOWIS_SERVER_EPANET_BUILD_TESTS=ON \
    "$@"
cmake --build build-linux-tests --parallel

ctest --test-dir build-linux-tests --output-on-failure
