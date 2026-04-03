/*
 * cpc — Optimizer
 * compiler/src/optimizer/optimizer.cp
 * =====================================
 * Optional optimization passes run on the typed AST before code generation.
 * All passes are zero-cost to skip — safe defaults produce correct code.
 *
 * Passes (in order):
 *   1. Constant folding     — evaluate compile-time expressions
 *   2. Dead code elimination — remove unreachable branches
 *   3. Constant propagation  — replace variables with their constant values
 *   4. Strength reduction    — replace expensive ops with cheaper ones
 *   5. Inline expansion      — inline small functions (future)
 */

import core;
import "ast/ast";
import "lexer/token";

/* ─── Optimization Level ──────────────────────────────────────────────────── */
enum OptLevel {
    O0,    /* no optimization — for debug builds */
    O1,    /* constant folding + dead code only */
    O2,    /* all passes — for release builds */
    Os,    /* optimize for size */
}

/* ─── Pass 1: Constant Folding ────────────────────────────────────────────── */
/*
 * Evaluates binary expressions where both operands are compile-time constants.
 * Example: 2 + 3  →  5   (at compile time, no runtime add needed)
 */

fn fold_constants(`mut ASTNode* node) -> void {
    if node == null { return; }

    /* Recurse first (bottom-up) */
    fold_constants(node.left);
    fold_constants(node.right);
    fold_constants(node.operand);
    fold_constants(node.if_cond);
    fold_constants(node.if_then);
    fold_constants(node.if_else);
    fold_constants(node.while_cond);
    for i in 0..node.item_count { fold_constants(node.items[i]); }
    for i in 0..node.arg_count  { fold_constants(node.args[i]);  }

    if node.kind != NODE_BINARY { return; }
    if node.left == null || node.right == null { return; }

    /* Both operands must be integer or float literals */
    bool both_int   = node.left.kind == NODE_INT_LIT   && node.right.kind == NODE_INT_LIT;
    bool both_float = node.left.kind == NODE_FLOAT_LIT && node.right.kind == NODE_FLOAT_LIT;

    if both_int {
        i64 a = node.left.lit_int;
        i64 b = node.right.lit_int;
        Option<i64> result = None;

        match node.op {
            TK_PLUS    -> result = Some(a + b),
            TK_MINUS   -> result = Some(a - b),
            TK_STAR    -> result = Some(a * b),
            TK_SLASH   -> {
                if b != 0 { result = Some(a / b); }
                /* Don't fold division by zero — let runtime catch it */
            },
            TK_PERCENT -> {
                if b != 0 { result = Some(a % b); }
            },
            TK_SHL     -> result = Some(a << (b as u32)),
            TK_SHR     -> result = Some(a >> (b as u32)),
            TK_BAND    -> result = Some(a & b),
            TK_BOR     -> result = Some(a | b),
            TK_BXOR    -> result = Some(a ^ b),
            _          -> {},
        }

        match result {
            Some(v) -> {
                /* Replace the binary node in-place with an int literal */
                node.kind    = NODE_INT_LIT;
                node.lit_int = v;
                node.left    = null;
                node.right   = null;
            },
            None -> {},
        }
    }

    if both_float {
        f64 a = node.left.lit_float;
        f64 b = node.right.lit_float;
        Option<f64> result = None;

        match node.op {
            TK_PLUS  -> result = Some(a + b),
            TK_MINUS -> result = Some(a - b),
            TK_STAR  -> result = Some(a * b),
            TK_SLASH -> { if b != 0.0 { result = Some(a / b); } },
            _        -> {},
        }

        match result {
            Some(v) -> {
                node.kind      = NODE_FLOAT_LIT;
                node.lit_float = v;
                node.left      = null;
                node.right     = null;
            },
            None -> {},
        }
    }

    /* Bool folding: !true → false, !false → true */
    if node.kind == NODE_UNARY && node.op == TK_NOT &&
       node.operand != null && node.operand.kind == NODE_BOOL_LIT {
        node.kind      = NODE_BOOL_LIT;
        node.lit_bool  = !node.operand.lit_bool;
        node.operand   = null;
    }
}

/* ─── Pass 2: Dead Code Elimination ──────────────────────────────────────── */
/*
 * Removes branches that can never execute.
 * Example:
 *   if false { do_stuff(); }   →   (removed entirely)
 *   if true  { x = 1; }       →   x = 1;
 */

fn eliminate_dead_code(`mut ASTNode* node) -> void {
    if node == null { return; }

    /* Recurse */
    for i in 0..node.item_count { eliminate_dead_code(node.items[i]); }
    if node.fn_body != null { eliminate_dead_code(node.fn_body); }

    if node.kind != NODE_IF { return; }
    if node.if_cond == null { return; }
    if node.if_cond.kind != NODE_BOOL_LIT { return; }

    if node.if_cond.lit_bool {
        /* if true { A } else { B }  →  A */
        node.kind      = NODE_BLOCK;
        node.items     = node.if_then != null ? node.if_then.items : null;
        node.item_count= node.if_then != null ? node.if_then.item_count : 0;
        node.if_cond   = null;
        node.if_then   = null;
        node.if_else   = null;
    } else {
        /* if false { A } else { B }  →  B (or empty block) */
        if node.if_else != null {
            node.kind       = NODE_BLOCK;
            node.items      = node.if_else.items;
            node.item_count = node.if_else.item_count;
        } else {
            node.kind       = NODE_BLOCK;
            node.item_count = 0;
        }
        node.if_cond = null;
        node.if_then = null;
        node.if_else = null;
    }
}

/* ─── Pass 3: Constant Propagation ───────────────────────────────────────── */
/*
 * Replaces uses of variables that are assigned exactly once with their value.
 * Example:
 *   const i32 X = 42;
 *   foo(X);   →   foo(42);
 */
fn propagate_constants(`mut ASTNode* node) -> void {
    /* Implementation tracks compile-time const values in a small map.
       For the bootstrap phase this is implemented in the C codegen directly. */
    if node == null { return; }
    for i in 0..node.item_count { propagate_constants(node.items[i]); }
    if node.fn_body != null { propagate_constants(node.fn_body); }
}

/* ─── Pass 4: Strength Reduction ─────────────────────────────────────────── */
/*
 * Replace expensive operations with cheaper ones.
 * Examples:
 *   x * 2   →   x + x         (multiply by 2 → add)
 *   x * 4   →   x << 2        (multiply by power-of-2 → shift)
 *   x / 4   →   x >> 2        (divide by power-of-2 → shift)
 */
fn strength_reduce(`mut ASTNode* node) -> void {
    if node == null { return; }

    fold_constants(node);  /* run fold first */
    for i in 0..node.item_count { strength_reduce(node.items[i]); }
    if node.fn_body != null { strength_reduce(node.fn_body); }
    if node.left  != null { strength_reduce(node.left);  }
    if node.right != null { strength_reduce(node.right); }

    if node.kind != NODE_BINARY { return; }
    if node.right == null || node.right.kind != NODE_INT_LIT { return; }

    i64 rhs = node.right.lit_int;

    /* x * N where N is a power of 2 → x << log2(N) */
    if node.op == TK_STAR && rhs > 0 && (rhs & (rhs - 1)) == 0 {
        i64 shift = 0;
        i64 v = rhs;
        while v > 1 { v = v >> 1; shift = shift + 1; }
        node.op              = TK_SHL;
        node.right.lit_int   = shift;
        return;
    }

    /* x / N where N is a power of 2 → x >> log2(N) */
    if node.op == TK_SLASH && rhs > 0 && (rhs & (rhs - 1)) == 0 {
        i64 shift = 0;
        i64 v = rhs;
        while v > 1 { v = v >> 1; shift = shift + 1; }
        node.op            = TK_SHR;
        node.right.lit_int = shift;
        return;
    }

    /* x * 2 → x + x */
    if node.op == TK_STAR && rhs == 2 {
        node.op    = TK_PLUS;
        node.right = node.left;   /* x + x */
    }
}

/* ─── Main optimizer entry point ─────────────────────────────────────────── */
fn optimize(`mut ASTNode* ast, OptLevel level) -> void {
    if level == OptLevel.O0 { return; }

    /* Always run constant folding */
    fold_constants(ast);

    if level == OptLevel.O1 {
        eliminate_dead_code(ast);
        return;
    }

    /* O2 / Os: all passes */
    eliminate_dead_code(ast);
    propagate_constants(ast);
    strength_reduce(ast);

    /* Second constant fold pass — propagation may have created new constants */
    fold_constants(ast);
    eliminate_dead_code(ast);
}
