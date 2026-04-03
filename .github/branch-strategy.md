# C-Prime Branch Strategy

## Branch Layout (mirrors Rust/Go/GCC)

```
main ─────────────────────────────────────────── stable releases only
  │
  ├── release/v0.1.0-alpha ◄── tagged v0.1.0-alpha (this release)
  │
develop ──────────────────────────────────────── integration branch
  │
  ├── feature/arm64-codegen     ← v0.2.0 ARM64 support
  ├── feature/generics          ← v0.3.0 full generics
  ├── feature/hashmap           ← v0.3.0 HashMap<K,V>
  ├── fix/borrow-checker        ← bug fixes
  └── hotfix/*                  ← emergency fixes to main
```

## Branch Rules

| Branch | Protected | Who can push | CI required |
|--------|-----------|-------------|-------------|
| `main` | ✅ Yes | Maintainer only (via PR) | ✅ All jobs |
| `develop` | ✅ Yes | PR only | ✅ Build + test |
| `release/*` | ✅ Yes | Maintainer only | ✅ All jobs |
| `feature/*` | ❌ No | Anyone | ⚡ Build only |
| `fix/*` | ❌ No | Anyone | ⚡ Build only |
| `hotfix/*` | ❌ No | Maintainer | ✅ All jobs |

## Release Flow

```
feature/* → develop → release/vX.Y.Z → main → tag vX.Y.Z
                                           ↓
                                      GitHub Release
                                      .deb + .vslnx + cpc binary
```

## Versioning

`v<major>.<minor>.<patch>[-alpha|-beta|-rc<N>]`

- `v0.1.0-alpha` — First public release (this one)
- `v0.1.0-beta`  — When borrow checker is complete  
- `v0.1.0`       — First stable release
- `v0.2.0-alpha` — ARM64 support
- `v0.3.0-alpha` — Full generics + HashMap
- `v1.0.0`       — Production ready

## How to release v0.1.0-alpha

```bash
# 1. Make sure you are on main with all changes committed
git checkout main
git pull origin main

# 2. Create the release branch
git checkout -b release/v0.1.0-alpha
git push origin release/v0.1.0-alpha

# 3. Tag the release
git tag -a v0.1.0-alpha -m "C-Prime v0.1.0-alpha — Backtick"
git push origin v0.1.0-alpha

# → GitHub Actions will automatically:
#   - Build everything
#   - Run all tests
#   - Create GitHub Release with .deb and .vslnx
#   - Create release/v0.1.0-alpha branch

# 4. Create dev branch (for ongoing work)
git checkout develop || git checkout -b develop
git push origin develop
```

## Initial repo setup (do this once after pushing)

```bash
# Enable branch protection via GitHub CLI:
gh api repos/{owner}/{repo}/branches/main/protection \
  --method PUT \
  --field required_status_checks='{"strict":true,"contexts":["Build & Self-Hosting Verification"]}' \
  --field enforce_admins=false \
  --field required_pull_request_reviews='{"required_approving_review_count":1}' \
  --field restrictions=null

gh api repos/{owner}/{repo}/branches/develop/protection \
  --method PUT \
  --field required_status_checks='{"strict":true,"contexts":["Build & Self-Hosting Verification"]}' \
  --field enforce_admins=false \
  --field required_pull_request_reviews=null \
  --field restrictions=null
```
