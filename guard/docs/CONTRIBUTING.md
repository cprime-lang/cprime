# Contributing to cpg (C-Prime Guard)

## What cpg checks for

- Memory leaks (CPG-M001–099)
- Double free / use-after-free (CPG-M004, CPG-M005)
- Format string injection (CPG-V004)
- Buffer overflows (CPG-V001)
- Hardcoded credentials (CPG-V010)
- Integer overflow in allocation sizes (CPG-V002)
- Unvalidated array indices (CPG-V006)

## Adding a new check

### Step 1 — Pick the right pass file

| What you're checking | File |
|---------------------|------|
| Heap allocation patterns | `src/memory/leak_detect.cp` |
| Security vulnerabilities | `src/vuln/vuln_scan.cp` |
| Borrow / ownership bugs | `src/analyzer/analyzer.cp` |

### Step 2 — Write the check function

```c
fn check_my_issue(`ASTNode* node, `mut Vec<Diagnostic> diags) -> void {
    if node.kind != NODE_CALL { return; }
    /* your pattern matching here */
    push_diag(diags, "CPG-V099", "CWE-XXX",
              DiagSeverity.Warning,
              node.span,
              "description of the issue",
              "suggested fix for the developer");
}
```

### Step 3 — Register it in scan_node()

Add a call to `check_my_issue(node, diags)` in the `scan_node()` function in
`src/vuln/vuln_scan.cp` (or the appropriate pass file).

### Step 4 — Write tests

```
guard/tests/my_check.safe.cp    -- should produce 0 diagnostics
guard/tests/my_check.unsafe.cp  -- should trigger your check
```

Run: `../../scripts/test.sh guard`

### Step 5 — Document it

Add an entry to `guard/docs/CHECKS.md` with:
- Code (CPG-Xnnn)
- CWE reference (if security-related)
- What it detects
- False positive rate / limitations
- Example of triggering code and safe alternative

## Test file format

`.safe.cp` files must compile and run without any CPG errors or warnings.
`.unsafe.cp` files must trigger at least one cpg diagnostic with the expected code.

Run the guard test suite:

```bash
./scripts/test.sh guard
```
