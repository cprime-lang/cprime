/*
 * cpc — Error Reporting
 * compiler/src/utils/error.cp
 * ============================
 * Centralized error reporting for the cpc compiler.
 * All compiler phases use these functions for consistent output.
 *
 * Output format (matches GCC/clang for editor integration):
 *   file.cp:12:5: error: message
 *   file.cp:12:5: warning: message
 *   file.cp:12:5: note: message
 *
 * Supports:
 *   - Colored terminal output (ANSI codes, disabled if not a TTY)
 *   - Error counts per phase
 *   - Source line quoting with caret pointing at the error
 *   - Error accumulation (report all errors, not just the first)
 */

import core;
import io;
import os;
import string;
import collections.vec;
import fmt;
import "lexer/token";

/* ─── Severity ────────────────────────────────────────────────────────────── */
enum Severity { Error, Warning, Note, Hint }

/* ─── Diagnostic ──────────────────────────────────────────────────────────── */
struct Diagnostic {
    Severity  sev;
    Span      span;
    str       message;
    str       source_line;  /* the actual source line for context */
    str       hint;         /* optional fix suggestion */
    str       code;         /* optional error code, e.g. "E001" */
}

/* ─── Error Reporter ──────────────────────────────────────────────────────── */
struct Reporter {
    Vec<Diagnostic> diags;
    u32             error_count;
    u32             warning_count;
    bool            use_color;
    bool            show_source;
    str             source_text;   /* full source, for line quoting */
}

fn Reporter.new(`str source, bool color) -> Reporter {
    return Reporter {
        diags:         Vec.new(),
        error_count:   0,
        warning_count: 0,
        use_color:     color,
        show_source:   true,
        source_text:   source,
    };
}

fn Reporter.add(`mut Reporter self, Severity sev, `Span span, `str msg) -> void {
    Diagnostic d;
    d.sev         = sev;
    d.span        = *span;
    d.message     = msg;
    d.source_line = get_source_line(self.source_text, span.line);
    d.hint        = "";
    d.code        = "";

    self.diags.push(d);
    if sev == Severity.Error   { self.error_count   = self.error_count   + 1; }
    if sev == Severity.Warning { self.warning_count = self.warning_count + 1; }
}

fn Reporter.error(`mut Reporter self, `Span span, `str msg) -> void {
    self.add(Severity.Error, span, msg);
}

fn Reporter.warn(`mut Reporter self, `Span span, `str msg) -> void {
    self.add(Severity.Warning, span, msg);
}

fn Reporter.note(`mut Reporter self, `Span span, `str msg) -> void {
    self.add(Severity.Note, span, msg);
}

fn Reporter.errorf(`mut Reporter self, `Span span, `str fmt_str, ...) -> void {
    str msg = fmt.vsprintf(fmt_str, ...);
    self.error(span, msg);
}

fn Reporter.flush(`Reporter self) -> void {
    for d in self.diags {
        print_diagnostic(`d, self.use_color);
    }
}

fn Reporter.has_errors(`Reporter self) -> bool {
    return self.error_count > 0;
}

fn Reporter.summary(`Reporter self) -> void {
    if self.error_count == 0 && self.warning_count == 0 { return; }
    io.eprintf("\n%d error(s), %d warning(s)\n",
               self.error_count, self.warning_count);
}

/* ─── Print one diagnostic ────────────────────────────────────────────────── */
fn print_diagnostic(`Diagnostic d, bool color) -> void {
    str RED    = if color { "\033[1;31m" } else { "" };
    str YELLOW = if color { "\033[1;33m" } else { "" };
    str CYAN   = if color { "\033[0;36m" } else { "" };
    str BOLD   = if color { "\033[1m"    } else { "" };
    str RESET  = if color { "\033[0m"    } else { "" };

    str sev_str = match d.sev {
        Severity.Error   -> fmt.sprintf("%serror%s",   RED,    RESET),
        Severity.Warning -> fmt.sprintf("%swarning%s", YELLOW, RESET),
        Severity.Note    -> fmt.sprintf("%snote%s",    CYAN,   RESET),
        Severity.Hint    -> fmt.sprintf("%shint%s",    CYAN,   RESET),
    };

    /* Location */
    io.eprintf("%s%s:%d:%d:%s %s: %s\n",
               BOLD,
               d.span.file, d.span.line, d.span.col,
               RESET,
               sev_str,
               d.message);

    /* Source line */
    if !string.is_empty(d.source_line) {
        io.eprintf("  %s\n", d.source_line);
        /* Caret pointing at column */
        str spaces = string.repeat(" ", (d.span.col + 1) as usize);
        str caret  = string.repeat("^", if d.span.len > 0 { d.span.len as usize } else { 1 });
        io.eprintf("  %s%s%s%s\n", spaces, RED, caret, RESET);
    }

    /* Hint */
    if !string.is_empty(d.hint) {
        io.eprintf("  %shint:%s %s\n", CYAN, RESET, d.hint);
    }
}

/* ─── Get source line by number ───────────────────────────────────────────── */
fn get_source_line(`str source, u32 line_number) -> str {
    u32 current_line = 1;
    usize line_start = 0;
    usize i = 0;
    usize src_len = string.len(source);

    while i < src_len {
        if current_line == line_number {
            /* Find end of this line */
            usize end = i;
            while end < src_len && string.char_at(source, end) != '\n' {
                end = end + 1;
            }
            return string.slice(source, i, end);
        }
        if string.char_at(source, i) == '\n' {
            current_line = current_line + 1;
            line_start   = i + 1;
        }
        i = i + 1;
    }
    return "";
}

/* ─── Compiler panic (internal errors) ───────────────────────────────────── */
fn ice(str msg) -> void {
    io.eprintf("\033[1;31m[ICE] Internal Compiler Error:\033[0m %s\n", msg);
    io.eprintf("Please report this at https://github.com/cprime-lang/cprime/issues\n");
    os.exit(101);
}

fn ice_at(`Span span, `str msg) -> void {
    io.eprintf("\033[1;31m[ICE] %s:%d:%d: Internal Compiler Error:\033[0m %s\n",
               span.file, span.line, span.col, msg);
    io.eprintf("Please report this at https://github.com/cprime-lang/cprime/issues\n");
    os.exit(101);
}
