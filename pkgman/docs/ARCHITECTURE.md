# cppm — Package Manager Architecture

## Overview

`cppm` is the C-Prime Package Manager. It handles everything from
installing libraries to checking for compiler updates.

## Component Map

```
cppm
├── cli/
│   ├── commands.cp     — All 13 subcommands (install, run, build, ...)
│   └── banner.cp       — ASCII art banner and help text
│
├── package/
│   ├── installer.cp    — Download, verify, and install packages
│   ├── manifest.cp     — cprime.json read/write
│   └── resolver.cp     — Dependency resolution (topological sort)
│
├── registry/
│   └── client.cp       — HTTP client for the cppm registry API
│
├── updater/
│   └── check.cp        — Background update checker with 24h cache
│
└── ui/
    ├── progress.cp     — Animated spinner and progress bar
    └── table.cp        — ASCII table renderer
```

## Package Format

A cppm package is a `.cppkg` file (a zip archive) with this structure:

```
mypackage-1.0.0.cppkg
├── cprime.json          (package manifest)
├── src/                 (C-Prime source files)
│   └── *.cp
├── docs/                (optional documentation)
└── tests/               (optional tests)
```

## Registry API

```
GET  /packages/:name              — package info
GET  /packages/:name/:version     — specific version
GET  /search?q=:query             — search packages
POST /packages                    — publish (requires auth token)
```

Registry URL: `https://registry.cprime-lang.org` (planned)
GitHub fallback: packages hosted as GitHub releases

## Dependency Resolution

cppm uses topological sort on the dependency graph:

1. Parse `cprime.json` to get direct dependencies
2. Fetch each dep's manifest to get their deps
3. Build directed acyclic graph (DAG)
4. Topological sort = installation order
5. Detect cycles → error

## Update Check Flow

```
cppm invoked
    │
    ▼
Is ~/.cppm/update_cache.json fresh (< 24h)?
    │
    ├─ YES → Show cached notification (if any), continue
    │
    └─ NO  → Spawn background thread
                │
                ▼
             HTTP GET github.com/cprime-lang/cprime/releases/latest
                │
                ▼
             Compare latest_version vs CPPM_VERSION
                │
                ├─ NEWER → Write notification to cache, print banner
                └─ SAME  → Write empty notification to cache
```
