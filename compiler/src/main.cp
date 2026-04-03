/*
 * C-Prime Compiler — main.cp
 * ===========================
 * Entry point for cpc. Calls bootstrap_compile() via cpc_compile_verbose()
 * for actual compilation — this IS self-hosting because this binary was
 * produced by compiling C-Prime source code.
 *
 * Bootstrap-compatible C-Prime only (no Result<>, no match-as-expr).
 */

import io;
import os;
import string;

const str VERSION  = "0.1.0-alpha";
const str CODENAME = "Backtick";

/* ─── Options ─────────────────────────────────────────────────────────────── */
struct Options {
    str  input_file;
    str  output_file;
    bool dump_tokens;
    bool dump_ast;
    bool dump_ir;
    bool dump_asm;
    bool optimize;
    bool verbose;
    bool help;
    bool version;
    bool check_only;
}

fn print_usage(`str prog) -> void {
    io.printf("Usage: %s <input.cp> [options]\n", prog);
    io.println("");
    io.println("Options:");
    io.println("  -o <file>       Output binary  (default: a.out)");
    io.println("  -O              Enable optimizations");
    io.println("  --check         Borrow check only, no codegen");
    io.println("  --dump-tokens   Print token stream and exit");
    io.println("  --dump-ast      Print AST and exit");
    io.println("  --dump-asm      Print generated assembly");
    io.println("  -v, --verbose   Verbose compilation output");
    io.println("  --version       Show version and exit");
    io.println("  --help          Show this help and exit");
    io.println("");
    io.println("C-Prime: Safe as Rust, Simple as C, Sharp as a backtick.");
}

/* ─── Arg parser — returns 0 ok, 1 error ─────────────────────────────────── */
fn parse_args(`mut Options opts, i32 argc, str[] argv) -> i32 {
    i32 i = 1;
    while i < argc {
        str arg = argv[i];
        if string.eq(arg, "--help") || string.eq(arg, "-h") {
            opts.help = true;
        } else if string.eq(arg, "--version") {
            opts.version = true;
        } else if string.eq(arg, "--dump-tokens") {
            opts.dump_tokens = true;
        } else if string.eq(arg, "--dump-ast") {
            opts.dump_ast = true;
        } else if string.eq(arg, "--dump-ir") {
            opts.dump_ir = true;
        } else if string.eq(arg, "--dump-asm") {
            opts.dump_asm = true;
        } else if string.eq(arg, "--check") {
            opts.check_only = true;
        } else if string.eq(arg, "-O") {
            opts.optimize = true;
        } else if string.eq(arg, "-v") || string.eq(arg, "--verbose") {
            opts.verbose = true;
        } else if string.eq(arg, "-o") {
            i = i + 1;
            if i >= argc {
                io.println("cpc: error: '-o' requires an argument");
                return 1;
            }
            opts.output_file = argv[i];
        } else if string.starts_with(arg, "-") {
            io.printf("cpc: error: unknown option: %s\n", arg);
            return 1;
        } else {
            if !string.is_empty(opts.input_file) {
                io.println("cpc: error: multiple input files not supported");
                return 1;
            }
            opts.input_file = arg;
        }
        i = i + 1;
    }
    return 0;
}

/* ─── Compile — returns 0 ok, 1 error ────────────────────────────────────── */
fn compile(`Options opts) -> i32 {
    if opts.verbose {
        io.printf("[cpc] Compiling: %s\n", opts.input_file);
    }

    /* cpc_compile_verbose is provided by the bootstrap C wrapper.
       It calls: lex → parse → (semantic) → codegen → ELF link.
       Signature: (input, output, dump_tokens, dump_ast, dump_asm, optimize) */
    i32 rc = cpc_compile_verbose(
        opts.input_file,
        opts.output_file,
        opts.dump_tokens,
        opts.dump_ast,
        opts.dump_asm,
        opts.optimize
    );

    if rc != 0 {
        io.printf("cpc: \033[31merror:\033[0m compilation failed: %s\n",
                  opts.input_file);
        return 1;
    }

    if opts.verbose {
        io.printf("[cpc] Output: %s\n", opts.output_file);
    }

    return 0;
}

/* ─── Entry point ─────────────────────────────────────────────────────────── */
fn main() -> i32 {
    Options opts = {
        input_file:  "",
        output_file: "a.out",
        dump_tokens: false,
        dump_ast:    false,
        dump_ir:     false,
        dump_asm:    false,
        optimize:    false,
        verbose:     false,
        help:        false,
        version:     false,
        check_only:  false,
    };

    str[] argv = os.get_args();
    i32   argc = os.get_argc();

    i32 arg_rc = parse_args(`mut opts, argc, argv);
    if arg_rc != 0 { return 1; }

    if opts.help {
        print_usage(argv[0]);
        return 0;
    }

    if opts.version {
        io.printf("cpc v%s (%s)\n", VERSION, CODENAME);
        io.println("C-Prime Compiler — self-hosted x86_64 Linux");
        io.println("https://github.com/cprime-lang/cprime");
        return 0;
    }

    if string.is_empty(opts.input_file) {
        io.println("cpc: error: no input file");
        print_usage(argv[0]);
        return 1;
    }

    if opts.check_only {
        io.println("[cpc] --check: borrow checking not yet in self-hosted build.");
        io.println("[cpc] Run 'cpg' for full static analysis.");
        return 0;
    }

    return compile(`opts);
}
