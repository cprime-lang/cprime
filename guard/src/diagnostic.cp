/*
 * cpg — Diagnostic Types
 * guard/src/diagnostic.cp
 * ========================
 * Shared types used across all cpg analysis passes.
 */

import core;
import "compiler/src/lexer/token";

enum DiagSeverity { Error, Warning, Info, Hint }

struct Diagnostic {
    str          code;      /* e.g. "CPG-M002" */
    str          cwe;       /* e.g. "CWE-120"  — empty if not security-related */
    DiagSeverity severity;
    Span         span;
    str          message;
    str          hint;      /* suggested fix */
    str          url;       /* link to docs */
}
