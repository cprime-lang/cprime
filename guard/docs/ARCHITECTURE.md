# cpg — C-Prime Guard Architecture

## Overview

`cpg` is C-Prime's static analysis tool. It finds memory leaks, borrow violations,
security vulnerabilities, and common bugs **before your code runs**.

## Pipeline

```
source.cp
    │
    ▼
┌──────────────────┐
│ Lexer + Parser   │  (shared with cpc)
│                  │  produces ASTNode tree
└────────┬─────────┘
         │
    ┌────┴─────────────────────────────────────────┐
    │                                              │
    ▼                                              ▼
┌──────────────┐                        ┌──────────────────┐
│ Memory Pass  │                        │ Vulnerability     │
│              │                        │ Scanner           │
│ - Leak detect│                        │                   │
│ - Double free│                        │ - Format strings  │
│ - Use-after  │                        │ - Buffer overflow │
│   free       │                        │ - Hardcoded creds │
└──────┬───────┘                        └────────┬──────────┘
       │                                         │
       └────────────────┬────────────────────────┘
                        │ Vec<Diagnostic>
                        ▼
               ┌────────────────┐
               │ Reporter       │
               │                │
               │ Human / JSON   │
               │ SARIF output   │
               └────────────────┘
```

## Diagnostic Code Ranges

| Range     | Category              |
|-----------|-----------------------|
| CPG-M001–099 | Memory safety      |
| CPG-V001–099 | Vulnerabilities    |
| CPG-B001–099 | Borrow violations  |
| CPG-T001–099 | Type issues        |
| CPG-S001–099 | Style/conventions  |

## Adding a New Check

1. Add a new function in the appropriate pass file:
   - Memory issues → `guard/src/memory/leak_detect.cp`
   - Security vulns → `guard/src/vuln/vuln_scan.cp`
   - Borrow issues  → `guard/src/analyzer/analyzer.cp`

2. Give it a `CPG-Xnnn` code and optionally a CWE reference

3. Add a test in `guard/tests/`:
   - `mycheck.safe.cp`   — should produce 0 diagnostics
   - `mycheck.unsafe.cp` — should trigger your check

4. Register it in the check runner in `guard/src/main.cp`

## File Map

| File | Responsibility |
|------|---------------|
| `src/main.cp` | CLI, pipeline orchestration |
| `src/diagnostic.cp` | Shared `Diagnostic` struct |
| `src/analyzer/analyzer.cp` | Parse + AST build entry point |
| `src/memory/leak_detect.cp` | Heap allocation tracking |
| `src/vuln/vuln_scan.cp` | Security vulnerability checks (CWE-mapped) |
| `src/reports/reporter.cp` | Human/JSON/SARIF output |
