/*
 * cpg — Report Emitter
 * guard/src/reports/reporter.cp
 * ==============================
 * Formats and emits cpg diagnostics in three output formats:
 *
 *   Human  — colored terminal output, matches GCC/clang style
 *   JSON   — machine-readable, used by VS Code extension
 *   SARIF  — Static Analysis Results Interchange Format v2.1.0
 *             used by GitHub Actions code scanning
 *
 * Usage:
 *   reporter_emit_human (`diags, filename, use_color);
 *   reporter_emit_json  (`diags, filename);
 *   reporter_emit_sarif (`diags, filename);
 */

import core;
import io;
import string;
import collections.vec;
import "guard/diagnostic";

const str CPRIME_VERSION = "0.1.0-alpha";
const str CPRIME_URL     = "https://github.com/cprime-lang/cprime";
const str SARIF_SCHEMA   = "https://schemastore.azurewebsites.net/schemas/json/sarif-2.1.0.json";
const str SARIF_VERSION  = "2.1.0";

/* ─── Human-readable output ───────────────────────────────────────────────── */
fn reporter_emit_human(`Vec<Diagnostic> diags, `str filename, bool color) -> void {
    str RED    = if color { "\033[1;31m" } else { "" };
    str YELLOW = if color { "\033[1;33m" } else { "" };
    str BLUE   = if color { "\033[0;34m" } else { "" };
    str GREEN  = if color { "\033[0;32m" } else { "" };
    str BOLD   = if color { "\033[1m"    } else { "" };
    str RESET  = if color { "\033[0m"    } else { "" };

    u32 errors   = 0;
    u32 warnings = 0;
    u32 hints    = 0;

    for d in diags {
        str sev_str = match d.severity {
            DiagSeverity.Error   -> fmt.sprintf("%serror%s",   RED,    RESET),
            DiagSeverity.Warning -> fmt.sprintf("%swarning%s", YELLOW, RESET),
            DiagSeverity.Info    -> fmt.sprintf("%sinfo%s",    BLUE,   RESET),
            DiagSeverity.Hint    -> fmt.sprintf("%shint%s",    GREEN,  RESET),
        };

        str code_str = if !string.is_empty(d.code) {
            fmt.sprintf("[%s] ", d.code)
        } else { "" };

        io.printf("%s%s:%d:%d:%s %s: %s%s\n",
                  BOLD,
                  d.span.file, d.span.line, d.span.col,
                  RESET,
                  sev_str,
                  code_str,
                  d.message);

        if !string.is_empty(d.hint) {
            io.printf("  %shint:%s %s\n", GREEN, RESET, d.hint);
        }
        if !string.is_empty(d.cwe) {
            io.printf("  %sCWE reference:%s %s — https://cwe.mitre.org/data/definitions/%s.html\n",
                      BLUE, RESET, d.cwe, string.slice(d.cwe, 4, string.len(d.cwe)));
        }
        io.println("");

        match d.severity {
            DiagSeverity.Error   -> errors   = errors   + 1,
            DiagSeverity.Warning -> warnings = warnings + 1,
            DiagSeverity.Hint    -> hints    = hints    + 1,
            _                    -> {},
        }
    }

    if diags.len() == 0 {
        io.printf("%s[cpg] No issues found in %s%s\n", GREEN, filename, RESET);
    } else {
        io.printf("[cpg] %s%d error(s)%s, %s%d warning(s)%s, %d hint(s)\n",
                  RED, errors, RESET,
                  YELLOW, warnings, RESET,
                  hints);
    }
}

/* ─── JSON output ─────────────────────────────────────────────────────────── */
fn reporter_emit_json(`Vec<Diagnostic> diags, `str filename) -> void {
    io.println("[");
    usize i = 0;
    while i < diags.len() {
        Diagnostic* d = diags.get(i);
        str comma = if i + 1 < diags.len() { "," } else { "" };
        str sev = match d.severity {
            DiagSeverity.Error   -> "error",
            DiagSeverity.Warning -> "warning",
            DiagSeverity.Info    -> "info",
            DiagSeverity.Hint    -> "hint",
        };
        io.printf(
            "  {\"file\":\"%s\",\"line\":%d,\"column\":%d,\"severity\":\"%s\","
            "\"code\":\"%s\",\"message\":%s,\"hint\":%s,\"cwe\":\"%s\"}%s\n",
            json_escape(d.span.file),
            d.span.line,
            d.span.col,
            sev,
            d.code,
            json_string(d.message),
            json_string(d.hint),
            d.cwe,
            comma
        );
        i = i + 1;
    }
    io.println("]");
}

/* ─── SARIF output ────────────────────────────────────────────────────────── */
fn reporter_emit_sarif(`Vec<Diagnostic> diags, `str filename) -> void {
    io.printf("{\n");
    io.printf("  \"$schema\": \"%s\",\n", SARIF_SCHEMA);
    io.printf("  \"version\": \"%s\",\n", SARIF_VERSION);
    io.printf("  \"runs\": [{\n");
    io.printf("    \"tool\": {\n");
    io.printf("      \"driver\": {\n");
    io.printf("        \"name\": \"cpg\",\n");
    io.printf("        \"version\": \"%s\",\n", CPRIME_VERSION);
    io.printf("        \"informationUri\": \"%s\",\n", CPRIME_URL);
    io.printf("        \"rules\": [\n");

    /* Emit unique rules */
    /* (simplified — in practice build a deduped rule table) */
    io.printf("          {\"id\": \"CPG-M002\", \"name\": \"PossibleMemoryLeak\",\n");
    io.printf("           \"shortDescription\": {\"text\": \"Possible memory leak\"}},\n");
    io.printf("          {\"id\": \"CPG-V004\", \"name\": \"FormatStringInjection\",\n");
    io.printf("           \"shortDescription\": {\"text\": \"Format string injection risk\"}}\n");
    io.printf("        ]\n");
    io.printf("      }\n");
    io.printf("    },\n");
    io.printf("    \"results\": [\n");

    usize i = 0;
    while i < diags.len() {
        Diagnostic* d = diags.get(i);
        str comma = if i + 1 < diags.len() { "," } else { "" };
        str level = match d.severity {
            DiagSeverity.Error   -> "error",
            DiagSeverity.Warning -> "warning",
            DiagSeverity.Info    -> "note",
            DiagSeverity.Hint    -> "note",
        };
        io.printf(
            "      {\"ruleId\":\"%s\",\"level\":\"%s\","
            "\"message\":{\"text\":%s},"
            "\"locations\":[{\"physicalLocation\":{\"artifactLocation\":"
            "{\"uri\":\"%s\"},\"region\":{\"startLine\":%d,\"startColumn\":%d}}}]}%s\n",
            d.code, level,
            json_string(d.message),
            json_escape(d.span.file),
            d.span.line, d.span.col,
            comma
        );
        i = i + 1;
    }

    io.printf("    ]\n");
    io.printf("  }]\n");
    io.printf("}\n");
}

/* ─── JSON helpers ────────────────────────────────────────────────────────── */
fn json_string(`str s) -> str {
    if string.is_empty(s) { return "\"\""; }
    return fmt.sprintf("\"%s\"", json_escape(s));
}

fn json_escape(`str s) -> str {
    /* Escape backslashes and double quotes */
    str result = "";
    usize i = 0;
    while i < string.len(s) {
        char c = string.char_at(s, i);
        if c == '"'  { result = string.concat(result, "\\\""); }
        else if c == '\\' { result = string.concat(result, "\\\\"); }
        else if c == '\n' { result = string.concat(result, "\\n"); }
        else if c == '\t' { result = string.concat(result, "\\t"); }
        else              { result = string.push_char(result, c); }
        i = i + 1;
    }
    return result;
}
