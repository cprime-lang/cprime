# C-Prime v0.1.0-alpha Release Notes — Codename: Backtick

## Downloads

| File | Description |
|------|-------------|
| `cprime_0.1.0_amd64.deb` | Debian/Ubuntu installer — cpc + cpg + cppm + stdlib |
| `cprime-lang-0.1.0.vslnx` | VS Code extension — syntax, hover, tutorial, releases |

## Install

```bash
sudo dpkg -i cprime_0.1.0_amd64.deb
cpc --version && cpg --version && cppm version
```

## What's Included

### cpc (Compiler)
- Self-hosted: written in C-Prime, compiles itself
- Compiles .cp → native x86_64 Linux ELF (no VM, no GC)
- Full borrow checker (7 rules, all compile-time)
- Type inference via `auto`
- Constant folding, dead code elimination (-O flag)
- Option<T>, Result<T,E>, generics, match, for..in

### cpg (Guard)
- 10 CWE-mapped security rules
- Memory leak detector
- Human / JSON / SARIF output formats
- --strict mode for CI pipelines

### cppm (Package Manager)
- cppm run, check, init, build, install, update
- Background update checker (cached 24h)
- Animated progress bar + table output
- GNOME/KDE/XFCE application menu entry
- .cp MIME type registration
- Bash completion for cppm and cpc

### Standard Library
- core, io, mem, string, fmt, os, math, collections.vec

### VS Code Extension
- Syntax highlighting (backtick glows red)
- Hover docs, 23 snippets, Run + Guard shortcuts
- Interactive 6-lesson tutorial with runnable examples
- Live release notes panel

## Known Limitations (Alpha)
- Linux x86_64 only (ARM64 in v0.2.0)
- Generics partially implemented
- No incremental compilation
- No DWARF debug info
- cppm registry not live yet

## Borrow Checker Quick Reference

```c
str a = "hello";
str b = a;           // MOVE — a is no longer valid
greet(`b);           // BORROW — b stays valid after call
modify(`mut b);      // MUTABLE BORROW — one at a time only
```
