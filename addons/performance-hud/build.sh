#!/usr/bin/env sh
# Cross-compiles the Performance HUD ReShade add-on with llvm-mingw.
# Get a Linux-hosted toolchain from https://github.com/mstorsjo/llvm-mingw/releases
# and point MINGW_PREFIX at its bin directory (defaults to a toolchain on PATH).
set -e
cd "$(dirname "$0")"

MINGW_PREFIX="${MINGW_PREFIX:-x86_64-w64-mingw32}"

"$MINGW_PREFIX-clang++" -O2 -std=c++20 -shared \
	-o PerformanceHUD.addon64 src/performance_hud.cpp \
	-Iinclude \
	-static \
	-lpdh \
	-Wl,--nxcompat,--dynamicbase,--high-entropy-va

echo "Built $(pwd)/PerformanceHUD.addon64"
