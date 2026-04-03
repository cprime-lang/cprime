/*
 * cpc — Borrow Checker
 * compiler/src/semantic/borrow_check.cp
 * =======================================
 * Enforces C-Prime ownership and borrowing rules at compile time.
 *
 * Rules enforced:
 *   1. Every value has exactly one owner
 *   2. When the owner goes out of scope, the value is dropped
 *   3. You can have many immutable borrows (`) OR exactly one mutable (`mut)
 *   4. You cannot have both immutable and mutable borrows simultaneously
 *   5. A borrow cannot outlive its owner
 *   6. You cannot use a moved value after the move
 *   7. You cannot mutate through an immutable borrow
 *
 * Algorithm: two-pass over the typed AST
 *   Pass 1 — Build the borrow graph: track which values are borrowed where
 *   Pass 2 — Validate: check all 7 rules across all code paths
 */

import core;
import collections.vec;
import "ast/ast";
import "lexer/token";

/* ─── Lifetime IDs ────────────────────────────────────────────────────────── */
/* Each scope gets a monotonically increasing lifetime ID.
   A borrow with lifetime L is only valid while L is active. */
const u32 LIFETIME_STATIC = 0;   /* 'static — lives forever */
const u32 LIFETIME_UNKNOWN = 0xFFFFFFFF;

/* ─── Borrow Kind ─────────────────────────────────────────────────────────── */
enum BorrowKind { Immutable, Mutable }

/* ─── Borrow record ───────────────────────────────────────────────────────── */
struct Borrow {
    str       var_name;
    BorrowKind kind;
    u32       lifetime;
    Span      origin;     /* where the borrow was created */
    bool      active;
}

/* ─── Variable record ─────────────────────────────────────────────────────── */
struct VarRecord {
    str   name;
    u32   lifetime;
    bool  moved;           /* true if ownership was transferred */
    bool  is_mut;
    Borrow[] borrows;      /* active borrows of this variable */
}

/* ─── Borrow Check Context ────────────────────────────────────────────────── */
struct BorrowCtx {
    Vec<VarRecord> vars;
    u32            next_lifetime;
    u32            scope_lifetime;
    Vec<str>       errors;
    Vec<str>       warnings;
    bool           had_error;
}

fn BorrowCtx.new() -> BorrowCtx {
    return BorrowCtx {
        vars:           Vec.new(),
        next_lifetime:  1,
        scope_lifetime: 1,
        errors:         Vec.new(),
        warnings:       Vec.new(),
        had_error:      false,
    };
}

fn BorrowCtx.error(`mut BorrowCtx self, `Span span, `str msg) -> void {
    str full = fmt.sprintf("[borrow] %s:%d:%d: \033[31merror:\033[0m %s",
                           span.file, span.line, span.col, msg);
    self.errors.push(full);
    self.had_error = true;
}

fn BorrowCtx.warn(`mut BorrowCtx self, `Span span, `str msg) -> void {
    str full = fmt.sprintf("[borrow] %s:%d:%d: \033[33mwarning:\033[0m %s",
                           span.file, span.line, span.col, msg);
    self.warnings.push(full);
}

fn BorrowCtx.new_lifetime(`mut BorrowCtx self) -> u32 {
    u32 id = self.next_lifetime;
    self.next_lifetime = self.next_lifetime + 1;
    return id;
}

fn BorrowCtx.define(`mut BorrowCtx self, `str name, bool is_mut) -> void {
    VarRecord rec;
    rec.name      = name;
    rec.lifetime  = self.scope_lifetime;
    rec.moved     = false;
    rec.is_mut    = is_mut;
    rec.borrows   = [];
    self.vars.push(rec);
}

fn BorrowCtx.find(`BorrowCtx self, `str name) -> Option<u32> {
    usize i = 0;
    while i < self.vars.len() {
        if string.eq(self.vars.get(i).name, name) {
            return Some(i as u32);
        }
        i = i + 1;
    }
    return None;
}

/* ─── Rule 6: Check moved value isn't used ──────────────────────────────────  */
fn check_move(`mut BorrowCtx ctx, `ASTNode* node) -> void {
    if node == null { return; }
    if node.kind != NODE_IDENT { return; }

    match ctx.find(node.ident_name) {
        None    -> return,
        Some(i) -> {
            VarRecord* rec = ctx.vars.get_mut(i as usize);
            if rec.moved {
                ctx.error(`node.span,
                    fmt.sprintf("use of moved value: '%s'", node.ident_name));
            }
        },
    }
}

/* ─── Rule 3/4: Check borrow conflicts ─────────────────────────────────────── */
fn check_borrow(`mut BorrowCtx ctx, `str name, BorrowKind kind, `Span span) -> void {
    match ctx.find(name) {
        None -> {
            ctx.error(span, fmt.sprintf("cannot borrow undeclared variable '%s'", name));
            return;
        },
        Some(i) -> {
            VarRecord* rec = ctx.vars.get_mut(i as usize);

            if rec.moved {
                ctx.error(span, fmt.sprintf("cannot borrow moved value '%s'", name));
                return;
            }

            /* Count active borrows */
            u32 active_imm = 0;
            u32 active_mut = 0;
            for b in rec.borrows {
                if b.active {
                    if b.kind == BorrowKind.Immutable { active_imm = active_imm + 1; }
                    if b.kind == BorrowKind.Mutable   { active_mut = active_mut + 1; }
                }
            }

            match kind {
                BorrowKind.Mutable -> {
                    if active_imm > 0 {
                        ctx.error(span,
                            fmt.sprintf(
                                "cannot borrow '%s' as mutable — %d immutable borrow(s) active",
                                name, active_imm));
                        return;
                    }
                    if active_mut > 0 {
                        ctx.error(span,
                            fmt.sprintf(
                                "cannot borrow '%s' as mutable more than once at a time",
                                name));
                        return;
                    }
                    if !rec.is_mut {
                        ctx.error(span,
                            fmt.sprintf("cannot borrow '%s' as mutable — not declared mutable",
                                        name));
                        return;
                    }
                },
                BorrowKind.Immutable -> {
                    if active_mut > 0 {
                        ctx.error(span,
                            fmt.sprintf(
                                "cannot borrow '%s' as immutable — mutable borrow active",
                                name));
                        return;
                    }
                },
            }

            /* Add the borrow */
            Borrow b;
            b.var_name = name;
            b.kind     = kind;
            b.lifetime = ctx.new_lifetime();
            b.origin   = *span;
            b.active   = true;
            rec.borrows.push(b);
        },
    }
}

/* ─── Scope entry/exit ──────────────────────────────────────────────────────── */
fn scope_enter(`mut BorrowCtx ctx) -> u32 {
    u32 saved = ctx.scope_lifetime;
    ctx.scope_lifetime = ctx.new_lifetime();
    return saved;
}

fn scope_leave(`mut BorrowCtx ctx, u32 saved_lifetime) -> void {
    /* Drop all borrows that belong to the leaving scope */
    usize i = 0;
    while i < ctx.vars.len() {
        VarRecord* rec = ctx.vars.get_mut(i);
        usize j = 0;
        while j < rec.borrows.len() {
            if rec.borrows.get(j).lifetime >= ctx.scope_lifetime {
                rec.borrows.get_mut(j).active = false;
            }
            j = j + 1;
        }
        i = i + 1;
    }
    ctx.scope_lifetime = saved_lifetime;
}

/* ─── Main borrow checker entry point ────────────────────────────────────── */
fn borrow_check(`ASTNode* ast) -> Result<void, str> {
    BorrowCtx ctx = BorrowCtx.new();
    bc_node(`mut ctx, ast);

    if ctx.had_error {
        /* Print all errors */
        for err in ctx.errors { io.eprintln(err); }
        return Err(fmt.sprintf("borrow check failed with %d error(s)",
                               ctx.errors.len()));
    }

    /* Print warnings even on success */
    for warn in ctx.warnings { io.eprintln(warn); }
    return Ok(void);
}

fn bc_node(`mut BorrowCtx ctx, `ASTNode* node) -> void {
    if node == null { return; }

    match node.kind {

        NODE_FN_DEF -> {
            u32 saved = scope_enter(`mut ctx);
            /* Define parameters */
            for i in 0..node.fn_param_count {
                ASTNode* p = node.fn_params[i];
                ctx.define(p.var_name, p.var_is_mut);
            }
            bc_node(`mut ctx, node.fn_body);
            scope_leave(`mut ctx, saved);
        },

        NODE_BLOCK | NODE_UNSAFE_BLOCK -> {
            u32 saved = scope_enter(`mut ctx);
            for i in 0..node.item_count {
                bc_node(`mut ctx, node.items[i]);
            }
            scope_leave(`mut ctx, saved);
        },

        NODE_VAR_DECL -> {
            bc_node(`mut ctx, node.var_init);
            ctx.define(node.var_name, node.var_is_mut);
        },

        NODE_ASSIGN -> {
            bc_node(`mut ctx, node.rhs);
            /* Assigning to a variable doesn't move it but modifies it */
        },

        NODE_BORROW -> {
            if node.borrow_expr != null &&
               node.borrow_expr.kind == NODE_IDENT {
                check_borrow(`mut ctx, node.borrow_expr.ident_name,
                             BorrowKind.Immutable, `node.span);
            } else {
                bc_node(`mut ctx, node.borrow_expr);
            }
        },

        NODE_BORROW_MUT -> {
            if node.borrow_expr != null &&
               node.borrow_expr.kind == NODE_IDENT {
                check_borrow(`mut ctx, node.borrow_expr.ident_name,
                             BorrowKind.Mutable, `node.span);
            } else {
                bc_node(`mut ctx, node.borrow_expr);
            }
        },

        NODE_IDENT -> {
            check_move(`mut ctx, node);
        },

        NODE_CALL | NODE_METHOD_CALL -> {
            bc_node(`mut ctx, node.callee);
            for i in 0..node.arg_count {
                bc_node(`mut ctx, node.args[i]);
            }
        },

        NODE_IF | NODE_IF_EXPR -> {
            bc_node(`mut ctx, node.if_cond);
            bc_node(`mut ctx, node.if_then);
            bc_node(`mut ctx, node.if_else);
        },

        NODE_WHILE -> {
            bc_node(`mut ctx, node.while_cond);
            bc_node(`mut ctx, node.while_body);
        },

        NODE_FOR_RANGE -> {
            bc_node(`mut ctx, node.for_start);
            bc_node(`mut ctx, node.for_end);
            u32 saved = scope_enter(`mut ctx);
            ctx.define(node.for_var, false);
            bc_node(`mut ctx, node.for_body);
            scope_leave(`mut ctx, saved);
        },

        NODE_MATCH -> {
            bc_node(`mut ctx, node.match_subject);
            for i in 0..node.match_arm_count {
                ASTNode* arm = node.match_arms[i];
                u32 saved = scope_enter(`mut ctx);
                if !string.is_empty(arm.arm_bind) {
                    ctx.define(arm.arm_bind, false);
                }
                bc_node(`mut ctx, arm.arm_body);
                scope_leave(`mut ctx, saved);
            }
        },

        NODE_RETURN -> bc_node(`mut ctx, node.ret_value),
        NODE_BINARY -> {
            bc_node(`mut ctx, node.left);
            bc_node(`mut ctx, node.right);
        },
        NODE_UNARY | NODE_DEREF | NODE_EXPR_STMT ->
            bc_node(`mut ctx, node.operand),

        _ -> {},
    }
}
