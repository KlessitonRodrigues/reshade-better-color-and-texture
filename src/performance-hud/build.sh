#!/usr/bin/env sh
# Cross-compiles the Performance HUD ReShade add-on with llvm-mingw.
#
# Resolution order:
#   1. MINGW_PREFIX environment variable (e.g. /opt/llvm-mingw/bin/x86_64-w64-mingw32)
#   2. A preinstalled "<prefix>-clang++" on PATH (distro mingw/llvm packages)
#   3. A pinned llvm-mingw release is downloaded to ~/.cache on first use,
#      extracted, and then used automatically.
#
# See https://github.com/mstorsjo/llvm-mingw/releases for available toolchains.
set -e
cd "$(dirname "$0")"

TOOLCHAIN_VERSION="20261104"
TOOLCHAIN_URL="https://github.com/mstorsjo/llvm-mingw/releases/download/${TOOLCHAIN_VERSION}/llvm-mingw-${TOOLCHAIN_VERSION}-ucrt-ubuntu-22.04-x86_64.tar.xz"
TOOLCHAIN_DIR="$HOME/.cache/llvm-mingw-${TOOLCHAIN_VERSION}-ucrt-ubuntu-22.04"

if [ -z "$MINGW_PREFIX" ] && ! command -v x86_64-w64-mingw32-clang++ >/dev/null 2>&1; then
	if [ ! -x "$TOOLCHAIN_DIR/bin/x86_64-w64-mingw32-clang++" ]; then
		echo "No llvm-mingw toolchain found; downloading pinned release ..." >&2
		mkdir -p "$TOOLCHAIN_DIR"
		curl -sL "$TOOLCHAIN_URL" | tar -xJf - -C "$TOOLCHAIN_DIR" --strip-components=1
	fi
	MINGW_PREFIX="$TOOLCHAIN_DIR/bin/x86_64-w64-mingw32"
fi

# Default to the distribution triplet when only MINGW_PREFIX was unset.

MINGW_PREFIX="${MINGW_PREFIX:-x86_64-w64-mingw32}"

# Output to the root reshade-shaders/Addons directory, where ReShade loads add-ons from.
OUT_DIR="$(pwd)/../../reshade-shaders/Addons"
mkdir -p "$OUT_DIR"

"$MINGW_PREFIX-clang++" -O2 -std=c++20 -shared \
	-o "$OUT_DIR/PerformanceHUD.addon64" src/performance_hud.cpp \
	-Iinclude \
	-static \
	-lpdh \
	-Wl,--nxcompat,--dynamicbase,--high-entropy-va

echo "Built $(cd "$OUT_DIR" && pwd)/PerformanceHUD.addon64"
