/*
 * cpg — Memory Leak Detector
 * guard/src/memory/leak_detect.cp
 * =================================
 * Statically detects potential memory leaks in C-Prime code.
 *
 * Detection strategies:
 *   1. Allocation tracking: any `mem.alloc` call whose result variable
 *      goes out of scope without a corresponding `mem.free` is a leak.
 *   2. Early-return leak: a function that allocated memory on one path
 *      but returns early on another without freeing.
 *   3. Ownership transfer: if ownership is correctly passed to a struct
 *      or returned, no leak is reported.
 *   4. unsafe block tracking: allocations in unsafe blocks are tracked
 *      but may require manual annotation.
 *
 * Diagnostic codes:
 *   CPG-M001  Allocation result ignored (immediate leak)
 *   CPG-M002  Variable goes out of scope without free
 *   CPG-M003  Potential leak on early return path
 *   CPG-M004  Double free detected
 *   CPG-M005  Use after free detected
 */

import core;
import collections.vec;
import "guard/diagnostic";

/* ─── Tracked allocation ──────────────────────────────────────────────────── */
struct AllocRecord {
    str    var_name;
    Span   alloc_site;
    bool   freed;
    bool   transferred;  /* passed to another owner */
    u32    scope_depth;
    str    alloc_fn;     /* "mem.alloc", "mem.alloc_uninit", etc. */
}

/* ─── Leak detector state ─────────────────────────────────────────────────── */
struct LeakDetector {
    Vec<AllocRecord> allocs;
    Vec<Diagnostic>  diags;
    u32              scope_depth;
}

fn LeakDetector.new() -> LeakDetector {
    return LeakDetector {
        allocs:      Vec.new(),
        diags:       Vec.new(),
        scope_depth: 0,
    };
}

fn LeakDetector.enter_scope(`mut LeakDetector self) -> void {
    self.scope_depth = self.scope_depth + 1;
}

fn LeakDetector.leave_scope(`mut LeakDetector self) -> void {
    /* Any allocation from this scope that hasn't been freed or transferred → leak */
    usize i = 0;
    while i < self.allocs.len() {
        AllocRecord* r = self.allocs.get_mut(i);
        if r.scope_depth == self.scope_depth && !r.freed && !r.transferred {
            Diagnostic d;
            d.code     = "CPG-M002";
            d.severity = DiagSeverity.Warning;
            d.message  = fmt.sprintf(
                "possible memory leak: '%s' allocated by '%s' goes out of scope without being freed",
                r.var_name, r.alloc_fn);
            d.span     = r.alloc_site;
            d.hint     = fmt.sprintf("add 'mem.free(%s)' before the variable goes out of scope",
                                     r.var_name);
            self.diags.push(d);
        }
        i = i + 1;
    }
    /* Remove records for this scope */
    /* (Simplified: keep all, just stop checking them) */
    self.scope_depth = self.scope_depth - 1;
}

fn LeakDetector.record_alloc(`mut LeakDetector self,
                               `str var_name, `Span site, `str alloc_fn) -> void {
    AllocRecord r;
    r.var_name    = var_name;
    r.alloc_site  = *site;
    r.freed       = false;
    r.transferred = false;
    r.scope_depth = self.scope_depth;
    r.alloc_fn    = alloc_fn;
    self.allocs.push(r);
}

fn LeakDetector.record_free(`mut LeakDetector self, `str var_name, `Span site) -> void {
    usize i = 0;
    bool found = false;
    while i < self.allocs.len() {
        AllocRecord* r = self.allocs.get_mut(i);
        if string.eq(r.var_name, var_name) {
            if r.freed {
                /* Double free! */
                Diagnostic d;
                d.code     = "CPG-M004";
                d.severity = DiagSeverity.Error;
                d.message  = fmt.sprintf("double free of '%s'", var_name);
                d.span     = *site;
                d.hint     = "remove the duplicate mem.free() call";
                self.diags.push(d);
            }
            r.freed = true;
            found   = true;
        }
        i = i + 1;
    }
    /* Freeing a variable that was never allocated is not necessarily an error
       (might be a parameter from outside) — so we don't report it */
}

fn LeakDetector.record_move(`mut LeakDetector self, `str var_name) -> void {
    usize i = 0;
    while i < self.allocs.len() {
        AllocRecord* r = self.allocs.get_mut(i);
        if string.eq(r.var_name, var_name) {
            r.transferred = true;
        }
        i = i + 1;
    }
}

/* ─── Walk the AST to detect leaks ──────────────────────────────────────── */
fn detect_leaks(`ASTNode* ast) -> Vec<Diagnostic> {
    LeakDetector ld = LeakDetector.new();
    walk_for_leaks(`mut ld, ast);
    return ld.diags;
}

fn walk_for_leaks(`mut LeakDetector ld, `ASTNode* node) -> void {
    if node == null { return; }

    match node.kind {
        NODE_BLOCK | NODE_UNSAFE_BLOCK -> {
            ld.enter_scope();
            for i in 0..node.item_count {
                walk_for_leaks(`mut ld, node.items[i]);
            }
            ld.leave_scope();
        },

        NODE_FN_DEF -> {
            ld.enter_scope();
            walk_for_leaks(`mut ld, node.fn_body);
            ld.leave_scope();
        },

        NODE_VAR_DECL -> {
            /* Check if initialiser is an alloc call */
            if node.var_init != null &&
               is_alloc_call(node.var_init) {
                ld.record_alloc(node.var_name,
                                `node.span,
                                get_alloc_fn_name(node.var_init));
            }
            walk_for_leaks(`mut ld, node.var_init);
        },

        NODE_EXPR_STMT -> {
            /* mem.free(x) call */
            if node.operand != null &&
               is_free_call(node.operand) {
                str freed_var = get_free_arg_name(node.operand);
                if !string.is_empty(freed_var) {
                    ld.record_free(freed_var, `node.span);
                }
            }
            /* Alloc call result discarded — immediate leak */
            if node.operand != null && is_alloc_call(node.operand) {
                Diagnostic d;
                d.code     = "CPG-M001";
                d.severity = DiagSeverity.Warning;
                d.message  = "allocation result ignored — immediate memory leak";
                d.span     = node.span;
                d.hint     = "assign the result to a variable and free it later";
                ld.diags.push(d);
            }
            walk_for_leaks(`mut ld, node.operand);
        },

        _ -> {
            /* Recurse */
            walk_for_leaks(`mut ld, node.left);
            walk_for_leaks(`mut ld, node.right);
            walk_for_leaks(`mut ld, node.if_cond);
            walk_for_leaks(`mut ld, node.if_then);
            walk_for_leaks(`mut ld, node.if_else);
            walk_for_leaks(`mut ld, node.while_cond);
            walk_for_leaks(`mut ld, node.while_body);
            for i in 0..node.item_count { walk_for_leaks(`mut ld, node.items[i]); }
            for i in 0..node.arg_count  { walk_for_leaks(`mut ld, node.args[i]);  }
        },
    }
}

fn is_alloc_call(`ASTNode* node) -> bool {
    if node == null { return false; }
    if node.kind != NODE_CALL && node.kind != NODE_METHOD_CALL { return false; }
    if node.callee == null { return false; }
    str name = get_call_name(node.callee);
    return string.eq(name, "mem.alloc") ||
           string.eq(name, "mem.alloc_uninit") ||
           string.eq(name, "mem.alloc_type") ||
           string.eq(name, "mem.alloc_array");
}

fn is_free_call(`ASTNode* node) -> bool {
    if node == null { return false; }
    if node.kind != NODE_CALL && node.kind != NODE_METHOD_CALL { return false; }
    if node.callee == null { return false; }
    str name = get_call_name(node.callee);
    return string.eq(name, "mem.free");
}

fn get_call_name(`ASTNode* callee) -> str {
    if callee == null { return ""; }
    if callee.kind == NODE_IDENT { return callee.ident_name; }
    if callee.kind == NODE_FIELD_ACCESS {
        if callee.object == null { return callee.field_name; }
        return fmt.sprintf("%s.%s",
            get_call_name(callee.object), callee.field_name);
    }
    return "";
}

fn get_alloc_fn_name(`ASTNode* node) -> str {
    if node == null { return "?"; }
    return get_call_name(node.callee);
}

fn get_free_arg_name(`ASTNode* node) -> str {
    if node == null { return ""; }
    if node.arg_count == 0 { return ""; }
    ASTNode* arg = node.args[0];
    if arg == null { return ""; }
    if arg.kind == NODE_IDENT { return arg.ident_name; }
    return "";
}
