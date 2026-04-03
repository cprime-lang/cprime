# cpc — Compiler Architecture

## Overview

`cpc` is the C-Prime compiler. It takes `.cp` source files and produces native ELF64 binaries for Linux x86_64.

```
source.cp
    │
    ▼
┌─────────────────┐
│ Lexer           │  lexer/lexer.cp + lexer/token.cp
│                 │  source text → TokenStream
└────────┬────────┘
         │ TokenStream
         ▼
┌─────────────────┐
│ Parser          │  parser/parser.cp + parser/expr.cp
│                 │  TokenStream → ASTNode tree
└────────┬────────┘
         │ ASTNode*
         ▼
┌─────────────────┐
│ Type Checker    │  semantic/type_check.cp
│                 │  infer types, check compatibility
└────────┬────────┘
         │ Typed ASTNode*
         ▼
┌─────────────────┐
│ Borrow Checker  │  semantic/borrow_check.cp
│                 │  enforce ownership + borrow rules
└────────┬────────┘
         │ Verified ASTNode*
         ▼
┌─────────────────┐
│ Optimizer       │  optimizer/optimizer.cp
│                 │  constant fold, dead code elim (optional)
└────────┬────────┘
         │ Optimized ASTNode*
         ▼
┌─────────────────┐
│ Code Generator  │  codegen/codegen.cp
│                 │  ASTNode* → x86_64 machine code
└────────┬────────┘
         │ .text + .rodata buffers
         ▼
┌─────────────────┐
│ ELF Writer      │  codegen/x86_64/elf.cp
│                 │  machine code → ELF64 binary via GNU as + ld
└────────┬────────┘
         │
         ▼
      a.out (ELF64 binary)
```

## File Map

| File | Responsibility |
|------|---------------|
| `src/main.cp` | Entry point, CLI argument parsing, pipeline orchestration |
| `src/lexer/token.cp` | Token type definitions, TokenStream struct |
| `src/lexer/lexer.cp` | Tokenizer: source text → TokenStream |
| `src/parser/parser.cp` | Recursive descent parser (top-level items, statements) |
| `src/parser/expr.cp` | Expression parser (Pratt parser for precedence climbing) |
| `src/ast/ast.cp` | ASTNode struct, all node kinds, ast_walk, ast_dump |
| `src/ast/types.cp` | TypeInfo struct, type utilities |
| `src/semantic/type_check.cp` | Type inference and compatibility checking |
| `src/semantic/borrow_check.cp` | Ownership and borrow rule enforcement |
| `src/semantic/semantic.cp` | Top-level semantic analysis coordinator |
| `src/optimizer/optimizer.cp` | Optimization passes (folding, DCE, strength reduction) |
| `src/codegen/codegen.cp` | AST → x86_64 machine code (single-pass) |
| `src/codegen/x86_64/emit.cp` | Raw x86_64 instruction emitters |
| `src/codegen/x86_64/elf.cp` | ELF binary writer (assembles via `as`, links via `ld`) |
| `src/utils/error.cp` | Unified error reporting with source quoting |
| `src/utils/arena.cp` | Arena allocator for AST nodes |

## Memory Model

The compiler uses a **CompileArena** for all AST allocations. When compilation completes (success or failure), the arena is destroyed in one call — no individual `free()` calls needed.

## Adding a New Language Feature

1. Add token(s) in `lexer/token.cp` if needed
2. Teach the lexer to produce them in `lexer/lexer.cp`
3. Add AST node kind(s) in `ast/ast.cp`
4. Parse the construct in `parser/parser.cp` or `parser/expr.cp`
5. Add type rules in `semantic/type_check.cp`
6. Add borrow rules in `semantic/borrow_check.cp` if needed
7. Add code generation in `codegen/codegen.cp`
8. Write a test in `tests/integration/`
