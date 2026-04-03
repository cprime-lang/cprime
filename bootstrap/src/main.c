/*
 * C-Prime Bootstrap Compiler — main.c
 * ====================================
 * This is the TEMPORARY C-based compiler used in Phase 2.
 * It compiles a subset of C-Prime syntax into x86_64 machine code.
 * Once the real C-Prime compiler (compiler/src/) is written,
 * this file is compiled by the real compiler and then DISCARDED.
 *
 * Pipeline:
 *   .cp source → Lexer → Parser → AST → x86_64 Codegen → ELF binary
 *
 * This bootstrap compiler intentionally supports only what is needed
 * to compile compiler/src/main.cp (the self-hosted compiler).
 * It is NOT a full C-Prime implementation.
 *
 * Phase 2 workflow:
 *   1. gcc -o cpc-bootstrap bootstrap/src/main.c [other files]
 *   2. ./cpc-bootstrap compiler/src/main.cp -o cpc
 *   3. ./cpc compiler/src/main.cp -o cpc2    (verify self-hosting)
 *   4. diff cpc cpc2                          (should be identical)
 *   5. rm -rf bootstrap/                      (discard bootstrap)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <assert.h>

#include "../../bootstrap/include/lexer.h"
#include "../../bootstrap/include/parser.h"
#include "../../bootstrap/include/codegen.h"

#define CPRIME_BOOTSTRAP_VERSION "0.1.0-bootstrap"
#define MAX_SOURCE_SIZE (10 * 1024 * 1024)  /* 10 MB */

/* ─── CLI Options ─────────────────────────────────────────────────────────── */
typedef struct {
    const char* input_file;    /* Source .cp file */
    const char* output_file;   /* Output binary */
    bool        dump_tokens;   /* --dump-tokens: print lexer output */
    bool        dump_ast;      /* --dump-ast: print AST */
    bool        dump_asm;      /* --dump-asm: print generated assembly */
    bool        verbose;       /* -v: verbose output */
    bool        help;          /* --help */
    bool        version;       /* --version */
} Options;

/* ─── Utilities ───────────────────────────────────────────────────────────── */
static void die(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "\033[31m[cpc-bootstrap] error:\033[0m ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(1);
}

static void info(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "\033[36m[cpc-bootstrap]\033[0m ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

/* ─── Source File Reader ──────────────────────────────────────────────────── */
static char* read_source_file(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) die("cannot open file: %s", path);

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size > MAX_SOURCE_SIZE) {
        fclose(f);
        die("source file too large: %s (%ld bytes)", path, size);
    }

    char* buf = malloc(size + 1);
    if (!buf) die("out of memory");

    size_t n = fread(buf, 1, size, f);
    fclose(f);

    buf[n] = '\0';
    *out_len = n;
    return buf;
}

/* ─── Argument Parser ─────────────────────────────────────────────────────── */
static void print_usage(const char* prog) {
    printf("Usage: %s <input.cp> [options]\n", prog);
    printf("\n");
    printf("Options:\n");
    printf("  -o <file>       Output file (default: a.out)\n");
    printf("  --dump-tokens   Print token stream and exit\n");
    printf("  --dump-ast      Print AST and exit\n");
    printf("  --dump-asm      Print generated assembly\n");
    printf("  -v, --verbose   Verbose compilation output\n");
    printf("  --version       Show compiler version\n");
    printf("  --help          Show this help message\n");
    printf("\n");
    printf("Bootstrap compiler — compiles a subset of C-Prime for self-hosting.\n");
}

static Options parse_args(int argc, char** argv) {
    Options opts = {0};
    opts.output_file = "a.out";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            opts.help = true;
        } else if (strcmp(argv[i], "--version") == 0) {
            opts.version = true;
        } else if (strcmp(argv[i], "--dump-tokens") == 0) {
            opts.dump_tokens = true;
        } else if (strcmp(argv[i], "--dump-ast") == 0) {
            opts.dump_ast = true;
        } else if (strcmp(argv[i], "--dump-asm") == 0) {
            opts.dump_asm = true;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            opts.verbose = true;
        } else if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) die("'-o' requires an argument");
            opts.output_file = argv[++i];
        } else if (argv[i][0] != '-') {
            if (opts.input_file) die("multiple input files not supported in bootstrap");
            opts.input_file = argv[i];
        } else {
            die("unknown option: %s", argv[i]);
        }
    }

    return opts;
}

/* ─── Main ────────────────────────────────────────────────────────────────── */
int main(int argc, char** argv) {
    Options opts = parse_args(argc, argv);

    if (opts.help) {
        print_usage(argv[0]);
        return 0;
    }

    if (opts.version) {
        printf("cpc-bootstrap version %s\n", CPRIME_BOOTSTRAP_VERSION);
        printf("C-Prime Bootstrap Compiler — Phase 2 temporary compiler\n");
        printf("This binary will be discarded after self-hosting is achieved.\n");
        return 0;
    }

    if (!opts.input_file) {
        fprintf(stderr, "Error: no input file specified.\n");
        print_usage(argv[0]);
        return 1;
    }

    /* ── Read source ── */
    size_t src_len = 0;
    char* source = read_source_file(opts.input_file, &src_len);
    if (opts.verbose) info("Read %zu bytes from '%s'", src_len, opts.input_file);

    /* ── Lex ── */
    if (opts.verbose) info("Lexing...");
    TokenStream* tokens = lex(source, src_len, opts.input_file);
    if (!tokens) die("Lexer failed.");

    if (opts.dump_tokens) {
        token_stream_dump(tokens);
        token_stream_free(tokens);
        free(source);
        return 0;
    }

    /* ── Parse ── */
    if (opts.verbose) info("Parsing...");
    ASTNode* ast = parse(tokens);
    if (!ast) die("Parser failed.");

    if (opts.dump_ast) {
        ast_dump(ast, 0);
        ast_free(ast);
        token_stream_free(tokens);
        free(source);
        return 0;
    }

    /* ── Code Generation ── */
    if (opts.verbose) info("Generating x86_64 code...");
    CodegenContext* ctx = codegen_create(opts.input_file, opts.output_file);
    if (!ctx) die("Codegen init failed.");

    int result = codegen_emit(ctx, ast);
    if (result != 0) die("Code generation failed.");

    if (opts.dump_asm) {
        codegen_dump_asm(ctx);
    }

    result = codegen_write_elf(ctx);
    if (result != 0) die("Failed to write output binary '%s'", opts.output_file);

    if (opts.verbose) info("Output written to '%s'", opts.output_file);

    /* ── Cleanup ── */
    codegen_free(ctx);
    ast_free(ast);
    token_stream_free(tokens);
    free(source);

    return 0;
}
