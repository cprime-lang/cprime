<div align="center">
  <h1>C` &nbsp;(C-Prime)</h1>
  <p><strong>Safe as Rust &nbsp;·&nbsp; Simple as C &nbsp;·&nbsp; Sharp as a backtick</strong></p>

  <p>
    <img src="https://img.shields.io/badge/version-0.1.0--alpha-orange" alt="version"/>
    <img src="https://img.shields.io/badge/platform-Linux%20x86__64-blue" alt="platform"/>
    <img src="https://img.shields.io/badge/license-MIT-green" alt="license"/>
    <img src="https://img.shields.io/badge/self--hosted-yes-brightgreen" alt="self-hosted"/>
    <img src="https://img.shields.io/badge/status-alpha-yellow" alt="status"/>
  </p>
</div>

---

## What is C`?

**C-Prime** is a statically typed, memory-safe systems programming language. It compiles directly to native x86_64 binaries with no VM and no garbage collector.

- **C-style syntax** — if you know C, you can read C-Prime immediately
- **Borrow operator `` ` ``** — the backtick borrows a value without taking ownership
- **Zero-cost safety** — all memory checks happen at compile time
- **Self-hosted** — the compiler is written in C-Prime and compiles itself

```c
import io;

// The ` borrow operator — the heart of C-Prime
fn greet(name `str) -> void {
    io.println(name);
}   // name is NOT freed — we only borrowed it

fn main() -> i32 {
    str message = "C-Prime";
    greet(`message);        // borrow with backtick
    io.println(message);    // still valid — borrow is over
    return 0;
}
```

---

## Tools

| Tool | Command | Description |
|------|---------|-------------|
| **cpc** | `cpc file.cp -o out` | Compiler — `.cp` → native ELF binary |
| **cpg** | `cpg file.cp` | Guard — memory leaks, borrow violations, CVEs |
| **cppm** | `cppm run file.cp` | Package manager — install libs, build, run |

---

## Install (Debian/Ubuntu)

```bash
sudo dpkg -i cprime_0.1.0_amd64.deb

cpc --version    # C-Prime Compiler 0.1.0-alpha
cpg --version    # C-Prime Guard 0.1.0-alpha
cppm version     # cppm 0.1.0-alpha
```

The installer also registers:
- **cppm** in your application menu (GNOME / KDE / XFCE)
- Bash tab-completion for `cpc`, `cpg`, `cppm`
- `.cp` file type in your file manager
- Man pages: `man cpc`, `man cpg`, `man cppm`

---

## VS Code Extension

Install `cprime-lang-0.1.0.vslnx` for:
- Syntax highlighting (the `` ` `` borrow operator glows red)
- C-Prime Dark theme
- Hover docs for all 17 types, all keywords, all stdlib functions
- 23 code snippets
- One-click run (`Ctrl+F5`) and guard check (`Ctrl+Shift+G`)
- Interactive 6-lesson tutorial panel
- Live release notes from GitHub

```bash
code --install-extension cprime-lang-0.1.0.vslnx
```

---

## Quick Start

```bash
# Create a new project
cppm init hello && cd hello

# Run it
cppm run src/main.cp

# Check for memory issues
cpg src/main.cp

# Build a release binary
cppm build
```

---

## Language Overview

```c
import io;
import math;
import collections.vec;

// Struct with method
struct Point { f64 x; f64 y; }

fn Point.distance(`Point self, `Point other) -> f64 {
    f64 dx = other.x - self.x;
    f64 dy = other.y - self.y;
    return math.sqrt(dx*dx + dy*dy);
}

// Option<T> — no nulls
fn find_item(i32 id) -> Option<str> {
    if id == 1 { return Some("Apple"); }
    return None;
}

// Result<T, E> — no error codes
fn safe_divide(i32 a, i32 b) -> Result<i32, str> {
    if b == 0 { return Err("division by zero"); }
    return Ok(a / b);
}

fn main() -> i32 {
    // Vec<T> with generic type
    Vec<i32> nums = Vec.new();
    for i in 0..5 { nums.push(i * 2); }

    // Pattern matching on Option
    match find_item(1) {
        Some(s) -> io.println(s),
        None    -> io.println("not found"),
    }

    // Pattern matching on Result
    match safe_divide(10, 3) {
        Ok(n)  -> io.printf("result: %d\n", n),
        Err(e) -> io.printf("error: %s\n", e),
    }

    return 0;
}
```

---

## The Borrow Rules

The compiler enforces these at compile time — zero runtime overhead:

| Rule | Description |
|------|-------------|
| One owner | Every value has exactly one owner |
| Drop on exit | Values are freed when their owner goes out of scope |
| Many `\`` borrows | You can have many immutable borrows at once |
| One `` `mut `` | OR exactly one mutable borrow |
| No mixing | Cannot mix mutable and immutable borrows |
| Lifetime | A borrow cannot outlive its owner |
| No use after move | Cannot use a value after it has been moved |

---

## Project Structure

```
cprime/
├── compiler/              # cpc — C-Prime Compiler (self-hosted)
│   ├── src/
│   │   ├── lexer/         # Tokenizer (token.cp, lexer.cp)
│   │   ├── parser/        # Parser (parser.cp, expr.cp)
│   │   ├── ast/           # AST node definitions
│   │   ├── semantic/      # Type checker + borrow checker
│   │   ├── optimizer/     # Optimization passes
│   │   ├── codegen/       # x86_64 code generator + ELF writer
│   │   └── utils/         # Error reporting, arena allocator
│   └── docs/              # Compiler architecture, contributing guide
├── guard/                 # cpg — C-Prime Guard
│   ├── src/
│   │   ├── memory/        # Leak detector, borrow checker
│   │   ├── vuln/          # Vulnerability scanner (CWE-mapped)
│   │   └── reports/       # Human / JSON / SARIF output
│   └── docs/
├── pkgman/                # cppm — Package Manager
│   ├── src/
│   │   ├── cli/           # All cppm commands (install, run, init…)
│   │   ├── updater/       # Background update checker
│   │   └── ui/            # Progress bar, table renderer
│   └── docs/
├── bootstrap/             # Temporary C compiler (Descarded after Phase 2)
│   ├── src/               # lexer.c, parser.c, codegen.c, elf_writer.c
│   └── include/           # codegen.h, lexer.h, parser.h
├── stdlib/                # Standard Library
│   ├── core/              # Option, Result, panic, assert
│   ├── io/                # File I/O, stdin/stdout
│   ├── mem/               # Heap allocation, Arena allocator
│   ├── string/            # String operations
│   ├── fmt/               # sprintf, formatting
│   ├── os/                # File system, process, env vars
│   ├── math/              # sqrt, trig, min/max, primes
│   ├── collections/       # Vec<T>, HashMap (coming)
│   └── net/               # HTTP client (coming)
├── vscode-extension/      # VS Code extension (TypeScript)
├── packaging/             # .deb packaging
├── docs/                  # Language spec, tutorials, examples
└── scripts/               # Build, test, package, release scripts
```

---

## Building from Source

```bash
# 1. Install dependencies
sudo ./scripts/setup.sh

# 2. Build the bootstrap C compiler
cd bootstrap && make && cd ..

# 3. Compile the real cpc using bootstrap
./build/bootstrap/cpc-bootstrap compiler/src/main.cp -o build/compiler/cpc

# 4. Verify self-hosting (cpc compiles itself)
./build/compiler/cpc compiler/src/main.cp -o build/compiler/cpc2
diff build/compiler/cpc build/compiler/cpc2  # must be identical

# 5. Compile cpg and cppm
./build/compiler/cpc guard/src/main.cp   -o build/guard/cpg
./build/compiler/cpc pkgman/src/main.cp  -o build/pkgman/cppm

# 6. Package everything
./scripts/package.sh

# Install
sudo dpkg -i dist/cprime_0.1.0_amd64.deb
```

---

## Standard Library

| Module | Import | Description |
|--------|--------|-------------|
| `core` | (auto) | Option, Result, panic, assert, swap |
| `io` | `import io;` | println, printf, file I/O, stdin |
| `mem` | `import mem;` | alloc, free, Arena allocator |
| `string` | `import string;` | eq, concat, slice, split, find |
| `fmt` | `import fmt;` | sprintf, pad, hex, bytes_human |
| `os` | `import os;` | exec, file ops, env vars, time |
| `math` | `import math;` | sqrt, sin, cos, PI, min, max |
| `collections.vec` | `import collections.vec;` | Vec<T> dynamic array |

---

## Roadmap

| Version | Goal |
|---------|------|
| **v0.1.0** | Compiler, Guard, Package Manager, VS Code extension ✅ |
| **v0.2.0** | ARM64 support, incremental compilation, `net` stdlib |
| **v0.3.0** | Full generics, traits, `HashMap<K,V>` |
| **v0.4.0** | cppm registry live, community packages |
| **v1.0.0** | Production-ready, complete stdlib, debugger support |

---

## Contributing

Read [docs/CONTRIBUTING.md](docs/CONTRIBUTING.md). PRs welcome.

The project is maintained anonymously. All communication via GitHub Issues and PRs.

---

## License

MIT — see [LICENSE](LICENSE)

---

*C-Prime: the language that borrows the best of everything.*
