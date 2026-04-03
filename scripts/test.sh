#!/usr/bin/env bash
# =============================================================================
# C-Prime Test Runner — scripts/test.sh
#
# Usage:
#   ./scripts/test.sh              # Run all tests
#   ./scripts/test.sh unit         # Compiler unit tests
#   ./scripts/test.sh integration  # Integration tests
#   ./scripts/test.sh stdlib       # Standard library tests
#   ./scripts/test.sh guard        # cpg tests
# =============================================================================

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
RESET='\033[0m'
BOLD='\033[1m'

CPC="./build/compiler/cpc"
CPG="./build/guard/cpg"
PASS=0
FAIL=0
SKIP=0

log_pass() { echo -e "  ${GREEN}✓${RESET} $1"; PASS=$((PASS + 1)); }
log_fail() { echo -e "  ${RED}✗${RESET} $1"; FAIL=$((FAIL + 1)); }
log_skip() { echo -e "  ${YELLOW}~${RESET} $1 (skipped)"; SKIP=$((SKIP + 1)); }
log_step() { echo -e "\n${BOLD}${CYAN}── $1 ──${RESET}"; }

# ─── Compiler Test Helper ─────────────────────────────────────────────────────
compile_test() {
    local name="$1"
    local file="$2"
    local expected_exit="${3:-0}"

    local tmp="/tmp/cprime_test_$$"
    local actual_exit=0

    $CPC "$file" -o "$tmp" 2>/dev/null || actual_exit=$?

    if [ "$actual_exit" -eq "$expected_exit" ]; then
        log_pass "$name"
        rm -f "$tmp"
        return 0
    else
        log_fail "$name (expected exit $expected_exit, got $actual_exit)"
        return 1
    fi
}

run_test() {
    local name="$1"
    local file="$2"
    local expected_output="$3"

    local tmp="/tmp/cprime_test_$$"

    if ! $CPC "$file" -o "$tmp" 2>/dev/null; then
        log_fail "$name (compilation failed)"
        return 1
    fi

    local actual
    actual=$(timeout 5 "$tmp" 2>/dev/null) || true

    if [ "$actual" = "$expected_output" ]; then
        log_pass "$name"
    else
        log_fail "$name"
        echo "    expected: '$expected_output'"
        echo "    actual:   '$actual'"
    fi
    rm -f "$tmp"
}

# ─── Unit Tests ───────────────────────────────────────────────────────────────
run_unit_tests() {
    log_step "Compiler Unit Tests"
    echo ""

    local test_dir="compiler/tests/unit"

    for f in "$test_dir"/*.cp; do
        [ -f "$f" ] || continue
        name=$(basename "$f" .cp)
        compile_test "unit/$name" "$f"
    done
}

# ─── Integration Tests ────────────────────────────────────────────────────────
run_integration_tests() {
    log_step "Integration Tests"
    echo ""

    local test_dir="compiler/tests/integration"

    # Each test has a .cp file and a .expected file
    for f in "$test_dir"/*.cp; do
        [ -f "$f" ] || continue
        name=$(basename "$f" .cp)
        expected_file="${f%.cp}.expected"

        if [ ! -f "$expected_file" ]; then
            log_skip "integration/$name (no .expected file)"
            continue
        fi

        expected=$(cat "$expected_file")
        run_test "integration/$name" "$f" "$expected"
    done
}

# ─── Stdlib Tests ─────────────────────────────────────────────────────────────
run_stdlib_tests() {
    log_step "Standard Library Tests"
    echo ""

    local stdlib_test_dir="docs/examples"

    for f in "$stdlib_test_dir"/*.cp; do
        [ -f "$f" ] || continue
        name=$(basename "$f" .cp)
        compile_test "stdlib/$name" "$f"
    done
}

# ─── Guard Tests ──────────────────────────────────────────────────────────────
run_guard_tests() {
    log_step "C-Prime Guard (cpg) Tests"
    echo ""

    local guard_test_dir="guard/tests"

    # Tests named *.safe.cp should pass with 0 errors
    for f in "$guard_test_dir"/*.safe.cp; do
        [ -f "$f" ] || continue
        name=$(basename "$f" .safe.cp)
        local exit_code=0
        $CPG "$f" --report=json > /dev/null 2>&1 || exit_code=$?
        if [ "$exit_code" -eq 0 ]; then
            log_pass "guard/safe/$name"
        else
            log_fail "guard/safe/$name (cpg reported errors on safe code)"
        fi
    done

    # Tests named *.unsafe.cp should fail (have errors)
    for f in "$guard_test_dir"/*.unsafe.cp; do
        [ -f "$f" ] || continue
        name=$(basename "$f" .unsafe.cp)
        local exit_code=0
        $CPG "$f" --report=json > /dev/null 2>&1 || exit_code=$?
        if [ "$exit_code" -ne 0 ]; then
            log_pass "guard/unsafe/$name (correctly detected issues)"
        else
            log_fail "guard/unsafe/$name (cpg missed unsafe code)"
        fi
    done
}

# ─── Summary ──────────────────────────────────────────────────────────────────
print_summary() {
    echo ""
    echo -e "${BOLD}Results:${RESET}"
    echo -e "  ${GREEN}Passed:${RESET}  $PASS"
    echo -e "  ${RED}Failed:${RESET}  $FAIL"
    echo -e "  ${YELLOW}Skipped:${RESET} $SKIP"
    echo ""

    if [ "$FAIL" -eq 0 ]; then
        echo -e "${GREEN}${BOLD}All tests passed!${RESET}"
        return 0
    else
        echo -e "${RED}${BOLD}$FAIL test(s) failed.${RESET}"
        return 1
    fi
}

# ─── Main ─────────────────────────────────────────────────────────────────────
SUITE="${1:-all}"

case "$SUITE" in
    all)
        run_unit_tests
        run_integration_tests
        run_stdlib_tests
        run_guard_tests
        ;;
    unit)        run_unit_tests ;;
    integration) run_integration_tests ;;
    stdlib)      run_stdlib_tests ;;
    guard)       run_guard_tests ;;
    *)
        echo "Unknown test suite: $SUITE"
        echo "Usage: $0 [all|unit|integration|stdlib|guard]"
        exit 1
        ;;
esac

print_summary
