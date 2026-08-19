#!/usr/bin/env bash
set -euo pipefail

model_args=()
if [[ -f ../AOWIS-SERVER-MODEL/CMakeLists.txt ]]; then
    model_args+=("-DAOWIS_SERVER_MODEL_SOURCE_DIR=../AOWIS-SERVER-MODEL")
fi

cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release \
    "${model_args[@]}" \
    "$@"
cmake --build build-linux --parallel
