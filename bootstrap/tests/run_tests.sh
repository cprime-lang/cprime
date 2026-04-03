#!/usr/bin/env bash
# =============================================================================
# C-Prime Bootstrap Test Suite
# bootstrap/tests/run_tests.sh
#
# Runs a battery of small C-Prime programs through the bootstrap compiler
# and checks their output. These are the minimum tests needed before
# we attempt to compile compiler/src/main.cp.
# =============================================================================

set -e

BOOTSTRAP="${1:-../build/bootstrap/cpc-bootstrap}"
PASS=0
FAIL=0

GREEN='\033[32m'
RED='\033[31m'
CYAN='\033[36m'
RESET='\033[0m'

check() {
    local name="$1"
    local src="$2"
    local expected="$3"

    local tmp_src="/tmp/cprime_test_$$_${name}.cp"
    local tmp_bin="/tmp/cprime_test_$$_${name}"

    printf "%s" "$src" > "$tmp_src"

    local actual
    if $BOOTSTRAP "$tmp_src" -o "$tmp_bin" 2>/dev/null && actual=$(timeout 3 "$tmp_bin" 2>/dev/null); then
        if [ "$actual" = "$expected" ]; then
            echo -e "  ${GREEN}✓${RESET} $name"
            PASS=$((PASS + 1))
        else
            echo -e "  ${RED}✗${RESET} $name"
            echo "    expected: '$expected'"
            echo "    actual:   '$actual'"
            FAIL=$((FAIL + 1))
        fi
    else
        echo -e "  ${RED}✗${RESET} $name (compile/run failed)"
        FAIL=$((FAIL + 1))
    fi

    rm -f "$tmp_src" "$tmp_bin"
}

check_lex_only() {
    local name="$1"
    local src="$2"
    local tmp_src="/tmp/cprime_test_$$_${name}.cp"
    printf "%s" "$src" > "$tmp_src"
    if $BOOTSTRAP --dump-tokens "$tmp_src" >/dev/null 2>&1; then
        echo -e "  ${GREEN}✓${RESET} lex/$name"
        PASS=$((PASS + 1))
    else
        echo -e "  ${RED}✗${RESET} lex/$name"
        FAIL=$((FAIL + 1))
    fi
    rm -f "$tmp_src"
}

check_parse_only() {
    local name="$1"
    local src="$2"
    local tmp_src="/tmp/cprime_test_$$_${name}.cp"
    printf "%s" "$src" > "$tmp_src"
    if $BOOTSTRAP --dump-ast "$tmp_src" >/dev/null 2>&1; then
        echo -e "  ${GREEN}✓${RESET} parse/$name"
        PASS=$((PASS + 1))
    else
        echo -e "  ${RED}✗${RESET} parse/$name"
        FAIL=$((FAIL + 1))
    fi
    rm -f "$tmp_src"
}

echo -e "${CYAN}=== C-Prime Bootstrap Compiler Tests ===${RESET}"
echo -e "${CYAN}  Bootstrap: $BOOTSTRAP${RESET}"
echo ""

# ─── Lexer Tests ──────────────────────────────────────────────────────────────
echo "Lexer tests:"
check_lex_only "empty"       'fn main() -> i32 { return 0; }'
check_lex_only "backtick"    'fn f(x `i32) -> void {} fn main() -> i32 { return 0; }'
check_lex_only "string"      'fn main() -> i32 { str s = "hello\nworld"; return 0; }'
check_lex_only "numbers"     'fn main() -> i32 { i32 x = 0xFF; i32 y = 0b1010; return 0; }'
check_lex_only "operators"   'fn main() -> i32 { i32 x = 1 + 2 * 3 - 4 / 2; return 0; }'
check_lex_only "comment"     '// line comment /* block */ fn main() -> i32 { return 0; }'
echo ""

# ─── Parser Tests ─────────────────────────────────────────────────────────────
echo "Parser tests:"
check_parse_only "fn_def"       'fn add(i32 a, i32 b) -> i32 { return a + b; } fn main() -> i32 { return 0; }'
check_parse_only "struct_def"   'struct Point { f64 x; f64 y; } fn main() -> i32 { return 0; }'
check_parse_only "if_else"      'fn main() -> i32 { if 1 > 0 { return 1; } else { return 0; } }'
check_parse_only "while"        'fn main() -> i32 { i32 x = 0; while x < 10 { x = x + 1; } return x; }'
check_parse_only "for_range"    'fn main() -> i32 { for i in 0..10 { } return 0; }'
check_parse_only "match"        'fn main() -> i32 { i32 x = 1; match x { 1 -> return 1, _ -> return 0, } }'
check_parse_only "borrow"       'fn f(`i32 x) -> void { } fn main() -> i32 { i32 y = 5; f(`y); return 0; }'
check_parse_only "const"        'const i32 MAX = 100; fn main() -> i32 { return MAX; }'
check_parse_only "some_ok"      'fn main() -> i32 { auto x = Some(42); auto y = Ok(0); return 0; }'
echo ""

# ─── Code Generation Tests ────────────────────────────────────────────────────
echo "Codegen tests:"

check "hello_world" \
    'import io;
fn main() -> i32 {
    io.println("Hello, World!");
    return 0;
}' \
    "Hello, World!"

check "return_value" \
    'fn main() -> i32 {
    return 42;
}' \
    ""  # (check exit code instead — this returns 42)

check "arithmetic_add" \
    'import io;
fn main() -> i32 {
    i32 x = 10 + 32;
    io.printf("%d\n", x);
    return 0;
}' \
    "42"

check "arithmetic_mul" \
    'import io;
fn main() -> i32 {
    i32 x = 6 * 7;
    io.printf("%d\n", x);
    return 0;
}' \
    "42"

check "if_true" \
    'import io;
fn main() -> i32 {
    if 1 == 1 {
        io.println("yes");
    } else {
        io.println("no");
    }
    return 0;
}' \
    "yes"

check "if_false" \
    'import io;
fn main() -> i32 {
    if 1 == 2 {
        io.println("yes");
    } else {
        io.println("no");
    }
    return 0;
}' \
    "no"

check "while_loop" \
    'import io;
fn main() -> i32 {
    i32 x = 0;
    while x < 3 {
        io.printf("%d\n", x);
        x = x + 1;
    }
    return 0;
}' \
    "0
1
2"

check "function_call" \
    'import io;
fn add(i32 a, i32 b) -> i32 {
    return a + b;
}
fn main() -> i32 {
    i32 result = add(10, 32);
    io.printf("%d\n", result);
    return 0;
}' \
    "42"

check "borrow_call" \
    'import io;
fn greet(`str name) -> void {
    io.printf("Hello, %s!\n", name);
}
fn main() -> i32 {
    str s = "World";
    greet(`s);
    return 0;
}' \
    "Hello, World!"

check "nested_if" \
    'import io;
fn main() -> i32 {
    i32 x = 5;
    if x > 3 {
        if x > 7 {
            io.println("big");
        } else {
            io.println("medium");
        }
    } else {
        io.println("small");
    }
    return 0;
}' \
    "medium"

check "comparison_ops" \
    'import io;
fn main() -> i32 {
    i32 a = 10;
    i32 b = 20;
    if a < b {
        io.println("less");
    }
    if a != b {
        io.println("neq");
    }
    return 0;
}' \
    "less
neq"

check "for_range" \
    'import io;
fn main() -> i32 {
    for i in 0..3 {
        io.printf("%d\n", i);
    }
    return 0;
}' \
    "0
1
2"

check "multiple_vars" \
    'import io;
fn main() -> i32 {
    i32 x = 1;
    i32 y = 2;
    i32 z = x + y;
    io.printf("%d\n", z);
    return 0;
}' \
    "3"

echo ""

# ─── Summary ──────────────────────────────────────────────────────────────────
TOTAL=$((PASS + FAIL))
echo "Results: $PASS/$TOTAL passed"
echo ""
if [ $FAIL -eq 0 ]; then
    echo -e "${GREEN}All tests passed! Ready for Phase 2 final step: make verify-selfhost${RESET}"
    exit 0
else
    echo -e "${RED}$FAIL test(s) failed.${RESET}"
    exit 1
fi
