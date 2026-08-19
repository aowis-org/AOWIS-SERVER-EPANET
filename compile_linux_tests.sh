#!/usr/bin/env bash
set -euo pipefail

model_args=()
if [[ -f ../AOWIS-SERVER-MODEL/CMakeLists.txt ]]; then
    model_args+=("-DAOWIS_SERVER_MODEL_SOURCE_DIR=../AOWIS-SERVER-MODEL")
fi

cmake -S . -B build-linux-tests -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DAOWIS_SERVER_EPANET_BUILD_SERVER=OFF \
    -DAOWIS_SERVER_EPANET_BUILD_TESTS=ON \
    "${model_args[@]}" \
    "$@"
cmake --build build-linux-tests --parallel

ctest --test-dir build-linux-tests --output-on-failure
