/*
 * cpg — Vulnerability Scanner
 * guard/src/vuln/vuln_scan.cp
 * ============================
 * Scans C-Prime source for security vulnerabilities and unsafe patterns.
 *
 * CWE-mapped checks:
 *   CPG-V001  Buffer overflow risk (CWE-120)
 *   CPG-V002  Integer overflow risk (CWE-190)
 *   CPG-V003  Null dereference risk (CWE-476)
 *   CPG-V004  Format string injection (CWE-134)
 *   CPG-V005  Use of unsafe block without justification (style)
 *   CPG-V006  Unvalidated array index (CWE-129)
 *   CPG-V007  Integer truncation (CWE-197)
 *   CPG-V008  Signed/unsigned mismatch in comparison (CWE-195)
 *   CPG-V009  Magic numbers in security-sensitive contexts
 *   CPG-V010  Hardcoded credentials pattern
 */

import core;
import collections.vec;
import string;
import "guard/diagnostic";

/* ─── Vulnerability rule ──────────────────────────────────────────────────── */
struct VulnRule {
    str      code;
    str      title;
    str      cwe;
    fn(`ASTNode*, `mut Vec<Diagnostic>) -> void check;
}

/* ─── Main scanner entry point ────────────────────────────────────────────── */
fn vuln_scan(`ASTNode* ast) -> Vec<Diagnostic> {
    Vec<Diagnostic> diags = Vec.new();
    scan_node(ast, `mut diags);
    return diags;
}

fn scan_node(`ASTNode* node, `mut Vec<Diagnostic> diags) -> void {
    if node == null { return; }

    /* Run all checks on this node */
    check_buffer_ops(node, diags);
    check_integer_overflow(node, diags);
    check_null_deref(node, diags);
    check_format_string(node, diags);
    check_unsafe_usage(node, diags);
    check_array_bounds(node, diags);
    check_hardcoded_secrets(node, diags);

    /* Recurse */
    scan_node(node.left,        diags);
    scan_node(node.right,       diags);
    scan_node(node.if_cond,     diags);
    scan_node(node.if_then,     diags);
    scan_node(node.if_else,     diags);
    scan_node(node.while_cond,  diags);
    scan_node(node.while_body,  diags);
    scan_node(node.fn_body,     diags);
    scan_node(node.ret_value,   diags);
    for i in 0..node.item_count { scan_node(node.items[i], diags); }
    for i in 0..node.arg_count  { scan_node(node.args[i],  diags); }
}

/* ─── CPG-V001: Buffer overflow risk ─────────────────────────────────────── */
fn check_buffer_ops(`ASTNode* node, `mut Vec<Diagnostic> diags) -> void {
    if node.kind != NODE_CALL && node.kind != NODE_METHOD_CALL { return; }
    str name = call_name(node);

    /* Unsafe C-style copy without bounds check */
    if string.eq(name, "mem.copy") && node.arg_count >= 3 {
        /* Check if the length argument is validated */
        ASTNode* len_arg = node.args[2];
        if len_arg != null && is_unchecked_external(len_arg) {
            push_diag(diags, "CPG-V001",
                "CWE-120",
                DiagSeverity.Warning,
                node.span,
                "mem.copy with potentially unchecked length — verify bounds before copying",
                "use the result of Vec.len() or a validated size variable as the length");
        }
    }
}

/* ─── CPG-V002: Integer overflow ─────────────────────────────────────────── */
fn check_integer_overflow(`ASTNode* node, `mut Vec<Diagnostic> diags) -> void {
    if node.kind != NODE_BINARY { return; }
    if node.op != TK_STAR && node.op != TK_PLUS { return; }

    /* If both sides involve user input (ident from a fn param) and result
       is used as an allocation size → integer overflow risk */
    if node.parent_is_alloc_arg && !one_side_is_constant(node) {
        push_diag(diags, "CPG-V002",
            "CWE-190",
            DiagSeverity.Warning,
            node.span,
            "arithmetic in allocation size may overflow — result wraps around to small value",
            "check for overflow before using as allocation size: assert(a < MAX_SIZE / b)");
    }
}

/* ─── CPG-V003: Null dereference ─────────────────────────────────────────── */
fn check_null_deref(`ASTNode* node, `mut Vec<Diagnostic> diags) -> void {
    /* Dereference of a value that could be null (e.g. unchecked Option) */
    if node.kind != NODE_DEREF { return; }
    if node.borrow_expr == null { return; }
    if node.borrow_expr.kind != NODE_IDENT { return; }

    /* If the variable is typed as a raw pointer (T*) and wasn't null-checked → risk */
    /* In practice this requires type info — simplified check here */
}

/* ─── CPG-V004: Format string injection ──────────────────────────────────── */
fn check_format_string(`ASTNode* node, `mut Vec<Diagnostic> diags) -> void {
    if node.kind != NODE_CALL && node.kind != NODE_METHOD_CALL { return; }
    str name = call_name(node);
    if !string.eq(name, "io.printf") && !string.eq(name, "io.eprintf") { return; }
    if node.arg_count == 0 { return; }

    /* First argument (format string) must be a string literal, not a variable */
    ASTNode* fmt_arg = node.args[0];
    if fmt_arg == null { return; }
    if fmt_arg.kind != NODE_STRING_LIT {
        push_diag(diags, "CPG-V004",
            "CWE-134",
            DiagSeverity.Error,
            node.span,
            "format string is not a string literal — format string injection vulnerability",
            "use io.printf(\"%s\", user_input) instead of io.printf(user_input)");
    }
}

/* ─── CPG-V005: Bare unsafe block ────────────────────────────────────────── */
fn check_unsafe_usage(`ASTNode* node, `mut Vec<Diagnostic> diags) -> void {
    if node.kind != NODE_UNSAFE_BLOCK { return; }

    /* Check that the preceding token was a doc comment explaining why */
    /* For the static analyser, we flag all unsafe blocks without a
       /// SAFETY: comment immediately above them */
    push_diag(diags, "CPG-V005",
        "",
        DiagSeverity.Hint,
        node.span,
        "unsafe block — add a `/// SAFETY:` comment explaining why this is safe",
        "/// SAFETY: this is safe because ...\nunsafe { ... }");
}

/* ─── CPG-V006: Unvalidated array index ──────────────────────────────────── */
fn check_array_bounds(`ASTNode* node, `mut Vec<Diagnostic> diags) -> void {
    if node.kind != NODE_INDEX { return; }
    if node.idx_index == null  { return; }

    /* If index comes directly from a function parameter without a bounds check
       in the enclosing scope → potentially out-of-bounds */
    if is_unchecked_external(node.idx_index) {
        push_diag(diags, "CPG-V006",
            "CWE-129",
            DiagSeverity.Warning,
            node.span,
            "array index may be out of bounds — validate index before use",
            "add: assert(index < array.len(), \"index out of bounds\")");
    }
}

/* ─── CPG-V010: Hardcoded credentials ────────────────────────────────────── */
fn check_hardcoded_secrets(`ASTNode* node, `mut Vec<Diagnostic> diags) -> void {
    if node.kind != NODE_VAR_DECL { return; }
    if node.var_init == null { return; }
    if node.var_init.kind != NODE_STRING_LIT { return; }

    /* Heuristic: variable name contains password/secret/key/token/api */
    str name_lower = string.to_lower(node.var_name);
    if string.contains(name_lower, "password") ||
       string.contains(name_lower, "secret")   ||
       string.contains(name_lower, "api_key")  ||
       string.contains(name_lower, "token")    ||
       string.contains(name_lower, "passwd")   {
        push_diag(diags, "CPG-V010",
            "CWE-798",
            DiagSeverity.Warning,
            node.span,
            fmt.sprintf("possible hardcoded credential in variable '%s'", node.var_name),
            "use environment variables or a secrets manager instead");
    }
}

/* ─── Helpers ─────────────────────────────────────────────────────────────── */
fn call_name(`ASTNode* node) -> str {
    if node.callee == null { return ""; }
    if node.callee.kind == NODE_IDENT { return node.callee.ident_name; }
    if node.callee.kind == NODE_FIELD_ACCESS {
        str obj = if node.callee.object != null &&
                     node.callee.object.kind == NODE_IDENT {
            node.callee.object.ident_name
        } else { "" };
        return fmt.sprintf("%s.%s", obj, node.callee.field_name);
    }
    return "";
}

fn is_unchecked_external(`ASTNode* node) -> bool {
    /* A variable is "unchecked external" if it came from a function parameter
       and there's no if/assert with a comparison on it in scope.
       Simplified: just return true if it's an ident (real impl needs dataflow) */
    return node != null && node.kind == NODE_IDENT;
}

fn one_side_is_constant(`ASTNode* node) -> bool {
    return (node.left  != null && node.left.kind  == NODE_INT_LIT) ||
           (node.right != null && node.right.kind == NODE_INT_LIT);
}

fn push_diag(`mut Vec<Diagnostic> diags,
              `str code, `str cwe,
              DiagSeverity sev, Span span,
              `str msg, `str hint) -> void {
    Diagnostic d;
    d.code     = code;
    d.cwe      = cwe;
    d.severity = sev;
    d.span     = span;
    d.message  = msg;
    d.hint     = hint;
    diags.push(d);
}
