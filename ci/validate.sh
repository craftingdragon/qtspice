#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-3.0-or-later
set -euo pipefail
c++ --version
cmake --version
pkg-config --modversion Qt6Core spice-client-glib-2.0 libswresample libavutil
export XDG_RUNTIME_DIR="$(mktemp -d)"
chmod 700 "$XDG_RUNTIME_DIR"
export QT_QPA_PLATFORM=xcb
export QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DQTSPICE_BUILD_WEBDAV_INTEGRATION_TESTS=OFF
cmake --build build --parallel 2
timeout 15m dbus-run-session -- xvfb-run -a ctest --test-dir build \
    --output-on-failure --timeout 120
mkdir -p build-consumer-src
cp examples/embedded-viewer/main.cpp build-consumer-src/main.cpp
cat > build-consumer-src/CMakeLists.txt <<'CMAKE'
cmake_minimum_required(VERSION 3.24)
project(Consumer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(QTSPICE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(QTSPICE_BUILD_PROXMOX OFF CACHE BOOL "" FORCE)
add_subdirectory(.. qtspice)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE QtSpice::QtSpice)
CMAKE
cmake -S build-consumer-src -B build-consumer -G Ninja \
    -DCMAKE_DISABLE_FIND_PACKAGE_Qt6Test=TRUE \
    -DCMAKE_DISABLE_FIND_PACKAGE_Qt6WebEngineWidgets=TRUE \
    -DCMAKE_DISABLE_FIND_PACKAGE_Qt6WebChannel=TRUE
cmake --build build-consumer --parallel 2
