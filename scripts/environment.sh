#!/usr/bin/env bash
# =============================================================================
# C-Prime Build Environment — scripts/environment.sh
# Source this before building or running any C-Prime tools.
#
# Usage:
#   source scripts/environment.sh
# =============================================================================

export CPRIME_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export CPRIME_VERSION="0.1.0-alpha"
export CPRIME_CODENAME="Backtick"

export CPRIME_BUILD_DIR="$CPRIME_ROOT/build"
export CPRIME_DIST_DIR="$CPRIME_ROOT/dist"
export CPRIME_STDLIB_DIR="$CPRIME_ROOT/stdlib"
export CPRIME_BOOTSTRAP_BIN="$CPRIME_BUILD_DIR/bootstrap/cpc-bootstrap"
export CPRIME_CPC="$CPRIME_BUILD_DIR/compiler/cpc"
export CPRIME_CPG="$CPRIME_BUILD_DIR/guard/cpg"
export CPRIME_CPPM="$CPRIME_BUILD_DIR/pkgman/cppm"

# Compiler flags used when building bootstrap
export CPRIME_CFLAGS="-O2 -std=c17 -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-unused-function"
export CPRIME_LDFLAGS="-lm"

# Add built binaries to PATH
export PATH="$CPRIME_BUILD_DIR/compiler:$CPRIME_BUILD_DIR/guard:$CPRIME_BUILD_DIR/pkgman:$PATH"

echo "[C-Prime] Environment loaded — v${CPRIME_VERSION} (${CPRIME_CODENAME})"
echo "[C-Prime] Root: $CPRIME_ROOT"
