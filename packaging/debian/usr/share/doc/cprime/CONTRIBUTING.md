# Contributing to C-Prime

Thank you for your interest in contributing to C-Prime!

---

## How to Contribute

### Reporting Bugs

Open a GitHub Issue with:
- Your OS and version
- The `.cp` source file that caused the issue (if possible)
- Full error output from `cpc` or `cpg`
- Expected vs actual behavior

### Suggesting Features

Open an issue tagged `enhancement`. Include:
- What problem it solves
- How it fits the language design goals (C-style syntax, memory safety, simplicity)
- Any similar features in other languages (Rust, C, Zig, etc.)

### Pull Requests

1. Fork the repo
2. Create a branch: `git checkout -b feature/my-feature`
3. Write code and tests
4. Ensure `./scripts/test.sh` passes
5. Open a PR with a clear description

---

## Code Style

**C-Prime source files (`.cp`):**
- 4-space indentation
- Snake case for functions and variables: `my_function`, `my_var`
- Pascal case for types and structs: `MyStruct`
- All caps for constants: `MAX_SIZE`
- Document public functions with `///` doc comments

**C bootstrap files:**
- Follow the existing style in `bootstrap/src/`
- C17 standard
- No undefined behavior

---

## What We Need Most

The project currently needs help with:

1. **More stdlib modules** — `net`, `crypto`, `thread`, `regex`, `os`
2. **Compiler test cases** — both valid and invalid C-Prime programs
3. **cpg diagnostic rules** — more vulnerability patterns
4. **cppm registry** — backend for package hosting
5. **Documentation** — tutorials, API docs, examples
6. **ARM64 codegen** — port the x86_64 code generator

---

## Design Principles

When contributing, keep these in mind:

1. **C-style syntax first** — if a C programmer can't read it, it's wrong
2. **Borrows are explicit** — the `` ` `` operator must be visible, never hidden
3. **Zero hidden cost** — no hidden allocations, no GC pauses
4. **Errors at compile time** — memory errors must be caught by the compiler, not at runtime
5. **Simple over clever** — prefer readable code over micro-optimizations

---

## Communication

The project maintainer is anonymous. All communication happens through GitHub Issues and Pull Requests.
