#!/bin/bash

cmake -S . -B build-linux
cmake --build build-linux

./build-linux/AOWIS-SERVER-GUI/aowis-server-gui
