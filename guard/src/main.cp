/*
 * cpg — C-Prime Guard
 * main.cp
 * ========
 * Static analysis tool for C-Prime source code.
 * Checks for:
 *   - Memory leaks
 *   - Use-after-free
 *   - Buffer overflows
 *   - Null dereferences
 *   - Borrow checker violations
 *   - Unused borrows
 *   - Uninitialized variable use
 *   - Integer overflow risks
 *   - Common security vulnerabilities (CWE-mapped)
 *
 * Usage:
 *   cpg <file.cp>
 *   cpg <file.cp> --report=json
 *   cpg <file.cp> --fix          (auto-fix safe issues)
 */

import io;
import os;
import fmt;
import string;
import collections.vec;

import "analyzer/analyzer";
import "memory/borrow_check";
import "memory/leak_detect";
import "vuln/vuln_scan";
import "reports/reporter";

const str CPG_VERSION = "0.1.0-alpha";

/* ─── Severity Levels ─────────────────────────────────────────────────────── */
enum Severity {
    ERROR,    /* Must fix — borrow violations, memory unsafety */
    WARNING,  /* Should fix — potential issues */
    INFO,     /* Informational */
    HINT,     /* Style suggestions */
}

/* ─── Diagnostic ──────────────────────────────────────────────────────────── */
struct Diagnostic {
    Severity    severity;
    str         code;       /* e.g., "CPG001" */
    str         message;
    str         file;
    i32         line;
    i32         column;
    str         hint;       /* Suggested fix */
    str         cwe;        /* CWE number if security issue */
}

/* ─── Report Format ───────────────────────────────────────────────────────── */
enum ReportFormat {
    HUMAN,     /* Colored terminal output */
    JSON,      /* JSON (for IDE integration) */
    SARIF,     /* SARIF (for GitHub Actions) */
}

/* ─── Options ─────────────────────────────────────────────────────────────── */
struct Options {
    str          input_file;
    ReportFormat format;
    bool         fix;        /* Auto-fix */
    bool         strict;     /* Treat warnings as errors */
    bool         verbose;
    bool         no_color;
    bool         help;
    bool         version;
}

fn severity_color(`Severity s) -> str {
    match *s {
        Severity.ERROR   -> "\033[31m",    /* Red */
        Severity.WARNING -> "\033[33m",    /* Yellow */
        Severity.INFO    -> "\033[34m",    /* Blue */
        Severity.HINT    -> "\033[32m",    /* Green */
    }
}

fn severity_name(`Severity s) -> str {
    match *s {
        Severity.ERROR   -> "error",
        Severity.WARNING -> "warning",
        Severity.INFO    -> "info",
        Severity.HINT    -> "hint",
    }
}

fn print_diagnostic(`Diagnostic d, bool use_color) -> void {
    str color  = if use_color { severity_color(`d.severity) } else { "" };
    str reset  = if use_color { "\033[0m" } else { "" };
    str bold   = if use_color { "\033[1m" } else { "" };

    /* Location */
    io.printf("%s%s:%d:%d:%s ", bold, d.file, d.line, d.column, reset);

    /* Severity and code */
    io.printf("%s%s[%s]%s: ", color, bold, d.code, reset);

    /* Message */
    io.printf("%s%s%s\n", bold, d.message, reset);

    /* Hint */
    if !string.is_empty(d.hint) {
        io.printf("  %shint:%s %s\n", "\033[32m", reset, d.hint);
    }

    /* CWE reference */
    if !string.is_empty(d.cwe) {
        io.printf("  %sCWE:%s %s\n", "\033[35m", reset, d.cwe);
    }
}

/* ─── Analysis Pipeline ───────────────────────────────────────────────────── */
fn analyze(`Options opts) -> Result<i32, str> {
    Result<str, str> source_result = os.read_file(opts.input_file);
    str source = match source_result {
        Ok(s)  -> s,
        Err(e) -> return Err(fmt.sprintf("cannot read file: %s", e)),
    };

    if opts.verbose {
        io.printf("[cpg] Analyzing: %s\n", opts.input_file);
    }

    Vec<Diagnostic> diagnostics = Vec.new();
    i32 error_count   = 0;
    i32 warning_count = 0;

    /* ── 1. Parse and build AST ── */
    Result<ASTNode, str> ast_result = analyzer_parse(source, opts.input_file);
    ASTNode ast = match ast_result {
        Ok(a)  -> a,
        Err(e) -> return Err(e),
    };

    /* ── 2. Borrow checker ── */
    if opts.verbose { io.println("[cpg] Running borrow checker..."); }
    Vec<Diagnostic> borrow_diags = borrow_check(`ast);
    Vec.append_all(`mut diagnostics, `borrow_diags);

    /* ── 3. Memory leak detection ── */
    if opts.verbose { io.println("[cpg] Checking for memory leaks..."); }
    Vec<Diagnostic> leak_diags = leak_detect(`ast);
    Vec.append_all(`mut diagnostics, `leak_diags);

    /* ── 4. Vulnerability scan ── */
    if opts.verbose { io.println("[cpg] Scanning for vulnerabilities..."); }
    Vec<Diagnostic> vuln_diags = vuln_scan(`ast);
    Vec.append_all(`mut diagnostics, `vuln_diags);

    /* ── 5. Emit report ── */
    bool use_color = !opts.no_color && opts.format == ReportFormat.HUMAN;

    if opts.format == ReportFormat.HUMAN {
        for diag in diagnostics {
            print_diagnostic(`diag, use_color);
            if diag.severity == Severity.ERROR   { error_count   = error_count + 1; }
            if diag.severity == Severity.WARNING { warning_count = warning_count + 1; }
        }

        io.println("");
        if error_count == 0 && warning_count == 0 {
            io.printf("\033[32m[cpg] No issues found in %s\033[0m\n", opts.input_file);
        } else {
            io.printf("[cpg] \033[31m%d error(s)\033[0m, \033[33m%d warning(s)\033[0m\n",
                error_count, warning_count);
        }
    } else if opts.format == ReportFormat.JSON {
        reporter_emit_json(`diagnostics, opts.input_file);
    } else if opts.format == ReportFormat.SARIF {
        reporter_emit_sarif(`diagnostics, opts.input_file);
    }

    /* Return non-zero exit code if there are errors (or warnings in strict mode) */
    if error_count > 0 { return Ok(1); }
    if opts.strict && warning_count > 0 { return Ok(1); }
    return Ok(0);
}

/* ─── Main ────────────────────────────────────────────────────────────────── */
fn main() -> i32 {
    Options opts = {
        input_file: "",
        format:     ReportFormat.HUMAN,
        fix:        false,
        strict:     false,
        verbose:    false,
        no_color:   false,
        help:       false,
        version:    false,
    };

    str[] argv = os.get_args();
    i32   argc = os.get_argc();

    i32 i = 1;
    while i < argc {
        str arg = argv[i];
        if string.eq(arg, "--help") || string.eq(arg, "-h") {
            opts.help = true;
        } else if string.eq(arg, "--version") {
            opts.version = true;
        } else if string.eq(arg, "--fix") {
            opts.fix = true;
        } else if string.eq(arg, "--strict") {
            opts.strict = true;
        } else if string.eq(arg, "--verbose") || string.eq(arg, "-v") {
            opts.verbose = true;
        } else if string.eq(arg, "--no-color") {
            opts.no_color = true;
        } else if string.eq(arg, "--report=json") {
            opts.format = ReportFormat.JSON;
        } else if string.eq(arg, "--report=sarif") {
            opts.format = ReportFormat.SARIF;
        } else if string.starts_with(arg, "-") {
            io.printf("cpg: unknown option: %s\n", arg);
            return 1;
        } else {
            opts.input_file = arg;
        }
        i = i + 1;
    }

    if opts.help {
        io.printf("cpg v%s — C-Prime Guard\n\n", CPG_VERSION);
        io.println("Usage: cpg <file.cp> [options]");
        io.println("");
        io.println("Options:");
        io.println("  --fix           Auto-fix safe issues");
        io.println("  --strict        Treat warnings as errors");
        io.println("  --report=json   Output JSON report");
        io.println("  --report=sarif  Output SARIF report (GitHub Actions)");
        io.println("  --no-color      Disable colored output");
        io.println("  -v, --verbose   Verbose analysis output");
        io.println("  --help          Show this message");
        io.println("  --version       Show version");
        return 0;
    }

    if opts.version {
        io.printf("cpg version %s\n", CPG_VERSION);
        io.println("C-Prime Guard — memory safety and vulnerability analyzer");
        return 0;
    }

    if string.is_empty(opts.input_file) {
        io.println("cpg: error: no input file");
        return 1;
    }

    match analyze(`opts) {
        Ok(code) -> return code,
        Err(e)   -> {
            io.printf("cpg: error: %s\n", e);
            return 1;
        },
    }
}
