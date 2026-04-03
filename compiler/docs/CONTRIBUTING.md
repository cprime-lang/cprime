# Contributing to cpc (C-Prime Compiler)

## Overview

`cpc` is the self-hosted C-Prime compiler. It is written entirely in C-Prime (`.cp` files).

## Getting Started

```bash
# Build the bootstrap compiler first (Phase 2)
cd bootstrap && make

# Then compile the real cpc
./build/bootstrap/cpc-bootstrap compiler/src/main.cp -o build/compiler/cpc
```

## Source Layout

```
compiler/src/
├── main.cp               ← Entry point + CLI
├── lexer/
│   ├── token.cp          ← Token types + TokenStream
│   └── lexer.cp          ← Tokenizer
├── parser/
│   ├── parser.cp         ← Statements + top-level parser
│   └── expr.cp           ← Pratt expression parser
├── ast/
│   └── ast.cp            ← All AST node definitions
├── semantic/
│   ├── type_check.cp     ← Type inference + checking
│   └── borrow_check.cp   ← Ownership + borrow enforcement
├── optimizer/
│   └── optimizer.cp      ← Optimization passes
├── codegen/
│   ├── codegen.cp        ← Main code generator
│   └── x86_64/
│       ├── emit.cp       ← Raw instruction emitters
│       └── elf.cp        ← ELF binary writer
└── utils/
    ├── error.cp          ← Error reporting
    └── arena.cp          ← Arena allocator
```

## Adding a Language Feature: Step-by-Step

Example: adding a `sizeof` operator

### Step 1 — Token

In `lexer/token.cp`, add to `TokenKind`:
```c
TK_SIZEOF,
```
In `lexer/lexer.cp`, add to `KEYWORDS[]`:
```c
{ word: "sizeof", kind: TK_SIZEOF },
```

### Step 2 — AST Node

In `ast/ast.cp`, add to `ASTKind`:
```c
NODE_SIZEOF,
```
Add field storage to `ASTNode`:
```c
ASTNode* sizeof_type;   /* the type being measured */
```

### Step 3 — Parse

In `parser/expr.cp`, in `parse_primary()`:
```c
if k == TK_SIZEOF {
    self.advance();
    self.expect(TK_LPAREN, "sizeof");
    ASTNode* n = ast_node(NODE_SIZEOF, loc);
    n.sizeof_type = self.parse_type();
    self.expect(TK_RPAREN, "sizeof");
    return n;
}
```

### Step 4 — Type check

In `semantic/type_check.cp`, `infer_type()`:
```c
NODE_SIZEOF -> return ty_usize(),  /* sizeof always returns usize */
```

### Step 5 — Code generation

In `codegen/codegen.cp`, `gen_expr()`:
```c
NODE_SIZEOF -> {
    /* Emit the size as a compile-time constant */
    usize size = get_type_size(n.sizeof_type);
    emit_mov_reg_imm64(`mut ctx.emit, RAX, size as i64);
},
```

### Step 6 — Test

Add `compiler/tests/integration/test_sizeof.cp`:
```c
import io;
fn main() -> i32 {
    io.printf("%d\n", sizeof(i32));   /* should print 4 */
    return 0;
}
```
Add `compiler/tests/integration/test_sizeof.expected`:
```
4
```
Run: `./scripts/test.sh integration`

## Coding Style

- 4-space indentation
- Snake case for functions and variables
- Pascal case for types and structs
- ALL_CAPS for constants
- Every public function must have a `///` doc comment
- Keep functions under 80 lines — split if longer

## Testing

```bash
./scripts/test.sh all          # all tests
./scripts/test.sh unit         # unit tests only
./scripts/test.sh integration  # integration tests
```

## Submitting Changes

1. Fork on GitHub
2. Create a feature branch
3. Add tests
4. Open a PR with description of what changed and why

All contributions are anonymous — use any username.
