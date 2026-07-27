#!/usr/bin/env bash
set -euo pipefail
git submodule foreach --recursive 'git remote set-url --push origin "$(git remote get-url origin | sed "s#https://github.com/#git@github.com:#")"'
