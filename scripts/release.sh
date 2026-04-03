#!/usr/bin/env bash
# =============================================================================
# C-Prime v0.1.0-alpha — Release Script
# scripts/release.sh
# =============================================================================
# Usage:
#   ./scripts/release.sh              — full release (tag + push + GitHub)
#   ./scripts/release.sh --dry-run    — print what would happen
#   ./scripts/release.sh --tag-only   — only create tag, don't push
# =============================================================================

set -euo pipefail

VERSION="0.1.0-alpha"
TAG="v${VERSION}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DRY=false
TAG_ONLY=false

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'

for arg in "$@"; do
  case "$arg" in
    --dry-run)   DRY=true ;;
    --tag-only)  TAG_ONLY=true ;;
  esac
done

log()  { echo -e "${CYAN}[release]${RESET} $*"; }
ok()   { echo -e "${GREEN}✓${RESET} $*"; }
warn() { echo -e "${YELLOW}⚠${RESET} $*"; }
err()  { echo -e "${RED}✗${RESET} $*"; exit 1; }
run()  { if $DRY; then echo -e "${YELLOW}[dry-run]${RESET} $*"; else eval "$*"; fi; }

echo -e "${BOLD}${CYAN}"
echo "╔════════════════════════════════════════════╗"
echo "║   C-Prime Release Script                   ║"
echo "║   Version: ${TAG}                     ║"
echo "╚════════════════════════════════════════════╝"
echo -e "${RESET}"

$DRY && warn "DRY RUN — no changes will be made\n"

# ─── Pre-flight ────────────────────────────────────────────────────────────
log "Pre-flight checks..."

[ -f "$ROOT/build/bootstrap/cpc-bootstrap" ] || \
  err "Bootstrap not built. Run: cd bootstrap && make"

[ -f "$ROOT/build/compiler/cpc" ] || \
  err "cpc not compiled. Run: ./build/bootstrap/cpc-bootstrap compiler/src/main.cp -o build/compiler/cpc"

command -v gh >/dev/null 2>&1 || \
  err "GitHub CLI not installed. Run: sudo apt install gh"

# Verify cpc version output
CPC_VER=$(./build/compiler/cpc --version 2>&1 | head -1)
echo "  cpc reports: $CPC_VER"
echo "$CPC_VER" | grep -q "$VERSION" || warn "cpc version doesn't match $VERSION"

ok "Pre-flight passed"

# ─── Git status ────────────────────────────────────────────────────────────
log "Checking git status..."
BRANCH=$(git rev-parse --abbrev-ref HEAD)
echo "  Current branch: $BRANCH"

if git status --porcelain | grep -q .; then
  warn "Uncommitted changes detected. Commit them before releasing."
  git status --short | head -10
  $DRY || { echo "Continue anyway? [y/N]"; read -r yn; [[ "$yn" == "y" ]] || exit 1; }
fi

ok "Git status OK"

# ─── Create tag ────────────────────────────────────────────────────────────
log "Creating tag $TAG..."

if git tag -l | grep -q "^${TAG}$"; then
  warn "Tag $TAG already exists"
else
  run git tag -a "$TAG" -m "C-Prime ${TAG} — Backtick"
  ok "Created tag $TAG"
fi

$TAG_ONLY && { ok "Tag-only mode — done."; exit 0; }

# ─── Push to GitHub ────────────────────────────────────────────────────────
log "Pushing to GitHub..."
run git push origin "$BRANCH"
run git push origin "$TAG"
ok "Pushed $BRANCH and $TAG"

# ─── Create release branch ─────────────────────────────────────────────────
log "Creating release branch release/${TAG}..."
RELEASE_BRANCH="release/${TAG}"

if git branch -a | grep -q "$RELEASE_BRANCH"; then
  warn "Branch $RELEASE_BRANCH already exists"
else
  run git checkout -b "$RELEASE_BRANCH"
  run git push origin "$RELEASE_BRANCH"
  run git checkout "$BRANCH"
  ok "Created branch $RELEASE_BRANCH"
fi

# ─── Ensure develop branch exists ──────────────────────────────────────────
log "Ensuring develop branch exists..."
if ! git branch -a | grep -q 'origin/develop'; then
  run git checkout -b develop
  run git push origin develop
  run git checkout "$BRANCH"
  ok "Created develop branch"
else
  ok "develop branch already exists"
fi

# ─── GitHub Release ────────────────────────────────────────────────────────
log "Creating GitHub Release $TAG..."

DEB="$ROOT/dist/cprime_${VERSION}_amd64.deb"
VSIX="$ROOT/dist/cprime-lang-${VERSION}.vsix"

if [ ! -f "$DEB" ]; then
  warn ".deb not found at $DEB — building packages first"
  run "$ROOT/scripts/package.sh"
fi

if $DRY; then
  echo -e "${YELLOW}[dry-run]${RESET} Would run:"
  echo "  gh release create $TAG \\"
  echo "    --title 'C-Prime ${TAG} — Backtick' \\"
  echo "    --notes-file docs/RELEASE_NOTES.md \\"
  echo "    --prerelease \\"
  echo "    $DEB \\"
  echo "    $VSLNX"
else
  gh release create "$TAG" \
    --title "C-Prime ${TAG} — Backtick" \
    --notes-file "$ROOT/docs/RELEASE_NOTES.md" \
    --prerelease \
    "$DEB" \
    "$VSIX"
  ok "GitHub Release created: https://github.com/$(gh repo view --json nameWithOwner -q .nameWithOwner)/releases/tag/$TAG"
fi

# ─── Summary ──────────────────────────────────────────────────────────────
echo ""
echo -e "${GREEN}${BOLD}════════════════════════════════════════════${RESET}"
echo -e "${GREEN}${BOLD} C-Prime ${TAG} released successfully!${RESET}"
echo -e "${GREEN}${BOLD}════════════════════════════════════════════${RESET}"
echo ""
echo "  Tag:             $TAG"
echo "  Release branch:  release/$TAG"
echo "  Develop branch:  develop"
echo "  .deb:            dist/cprime_${VERSION}_amd64.deb"
echo "  .vsix:          dist/cprime-lang-${VERSION}.vsix"
echo ""
echo -e "${BOLD}Next steps:${RESET}"
echo "  1. Post to Reddit (see docs/REDDIT_POST_DRAFT.md)"
echo "  2. Start feature/arm64-codegen branch for v0.2.0"
echo ""
