#!/usr/bin/env bash
# =============================================================================
# C-Prime — Phase 1 Setup Script
# setup.sh — Install all dependencies and verify the build environment
#
# Usage:
#   chmod +x scripts/setup.sh
#   ./scripts/setup.sh
#
# Supports: Debian 11/12, Ubuntu 20.04/22.04/24.04
# =============================================================================

set -e  # Exit on any error

# ─── Colors ──────────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
RESET='\033[0m'

# ─── Banner ───────────────────────────────────────────────────────────────────
print_banner() {
    echo -e "${CYAN}"
    cat << 'EOF'
   ______      ____       _
  / ____/     / __ \___  (_)___ ___  ___
 / /   ____  / /_/ / __)/ / __ `__ \/ _ \
/ /___/_____/ ____/ /  / / / / / / /  __/
\____/     /_/   /_/  /_/_/ /_/ /_/\___/

  Safe as Rust, Simple as C, Sharp as a backtick.
  Phase 1: Environment Setup
EOF
    echo -e "${RESET}"
}

# ─── Helpers ─────────────────────────────────────────────────────────────────
log_info()    { echo -e "${BLUE}[INFO]${RESET}  $1"; }
log_ok()      { echo -e "${GREEN}[OK]${RESET}    $1"; }
log_warn()    { echo -e "${YELLOW}[WARN]${RESET}  $1"; }
log_error()   { echo -e "${RED}[ERROR]${RESET} $1"; }
log_step()    { echo -e "\n${BOLD}${CYAN}──── $1 ────${RESET}"; }

check_root() {
    if [ "$EUID" -ne 0 ]; then
        log_error "Please run as root: sudo ./scripts/setup.sh"
        exit 1
    fi
}

check_distro() {
    log_step "Checking distribution"
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        log_info "Detected: $NAME $VERSION_ID"
        case "$ID" in
            debian|ubuntu|linuxmint|pop)
                log_ok "Supported distribution."
                ;;
            *)
                log_warn "Unsupported distro '$ID'. Proceeding anyway, but apt is required."
                ;;
        esac
    else
        log_error "Cannot detect distribution. /etc/os-release not found."
        exit 1
    fi
}

# ─── Dependency Install ──────────────────────────────────────────────────────
install_apt_packages() {
    log_step "Updating apt package lists"
    apt-get update -qq
    log_ok "Package lists updated."

    log_step "Installing build dependencies"

    PACKAGES=(
        # Core build tools
        "build-essential"       # gcc, g++, make
        "gcc"                   # C compiler (for bootstrap phase)
        "g++"                   # C++ (for some tooling)
        "make"                  # Build system
        "cmake"                 # CMake (optional, for some stdlib deps)
        "ninja-build"           # Fast build backend

        # Assembler and linker
        "nasm"                  # Netwide Assembler — for x86_64 codegen
        "binutils"              # ld, objdump, readelf, nm, ar, ranlib
        "lld"                   # LLVM linker (faster than GNU ld)

        # Code analysis and debugging
        "valgrind"              # Memory error detector (used by cpg)
        "gdb"                   # GNU Debugger
        "strace"                # System call tracer
        "ltrace"                # Library call tracer

        # Static analysis
        "cppcheck"              # C/C++ static analysis (reference for cpg)
        "clang"                 # Clang compiler (for comparison/testing)
        "clang-tools"           # Clang static analyzer

        # Libraries cprime stdlib will wrap
        "libssl-dev"            # OpenSSL (for stdlib/crypto and stdlib/net)
        "libcurl4-openssl-dev"  # libcurl (for cppm HTTP requests)
        "libjson-c-dev"         # JSON parsing (for cppm registry)
        "zlib1g-dev"            # zlib compression
        "libreadline-dev"       # Readline (for cppm CLI)

        # Packaging tools
        "dpkg-dev"              # .deb packaging tools
        "fakeroot"              # Build .deb as non-root
        "debhelper"             # Debian helper scripts
        "dh-make"               # Debian package template generator

        # Git and version control
        "git"                   # Version control
        "git-lfs"               # Large file support (for assets)

        # Node.js (for VS Code extension build)
        "nodejs"                # Node.js runtime
        "npm"                   # Node package manager

        # Documentation
        "doxygen"               # API doc generator
        "graphviz"              # Graph generation (for doxygen)
        "pandoc"                # Markdown → HTML/PDF

        # Testing
        "lcov"                  # Code coverage
        "python3"               # Test scripting
        "python3-pip"           # Python packages

        # Utilities
        "curl"                  # HTTP client (used in scripts)
        "wget"                  # File downloader
        "jq"                    # JSON processor (for cppm scripts)
        "xxd"                   # Hex dump (debugging codegen)
        "file"                  # File type detection
        "pkg-config"            # Library metadata
    )

    log_info "Installing ${#PACKAGES[@]} packages..."
    apt-get install -y "${PACKAGES[@]}" 2>&1 | grep -E "(Installing|Unpacking|Setting up|already)" || true
    log_ok "All apt packages installed."
}

# ─── Node.js Version Check ────────────────────────────────────────────────────
setup_nodejs() {
    log_step "Setting up Node.js"

    NODE_VER=$(node --version 2>/dev/null || echo "none")
    NODE_MAJOR=$(echo "$NODE_VER" | grep -oP '(?<=v)\d+' || echo "0")

    if [ "$NODE_MAJOR" -lt 18 ]; then
        log_warn "Node.js $NODE_VER is too old. Installing Node.js 20 LTS..."
        curl -fsSL https://deb.nodesource.com/setup_20.x | bash -
        apt-get install -y nodejs
        log_ok "Node.js $(node --version) installed."
    else
        log_ok "Node.js $NODE_VER is compatible."
    fi

    # Install global npm tools for VS Code extension
    log_info "Installing VS Code extension build tools..."
    npm install -g vsce typescript @types/node --silent
    log_ok "vsce and TypeScript installed globally."
}

# ─── Verify Installations ─────────────────────────────────────────────────────
verify_tools() {
    log_step "Verifying all tools"

    TOOLS=(
        "gcc:C Compiler"
        "make:Make"
        "nasm:Assembler"
        "ld:Linker"
        "lld:LLVM Linker"
        "valgrind:Memory Analyzer"
        "gdb:Debugger"
        "dpkg-deb:Debian Packager"
        "git:Git"
        "node:Node.js"
        "npm:NPM"
        "curl:cURL"
        "jq:JSON Processor"
        "pkg-config:pkg-config"
    )

    ALL_OK=true
    for ENTRY in "${TOOLS[@]}"; do
        TOOL="${ENTRY%%:*}"
        NAME="${ENTRY##*:}"
        if command -v "$TOOL" &>/dev/null; then
            VER=$(eval "$TOOL --version 2>/dev/null | head -1" || echo "?")
            log_ok "$NAME: $VER"
        else
            log_error "$NAME ($TOOL) NOT FOUND"
            ALL_OK=false
        fi
    done

    if [ "$ALL_OK" = false ]; then
        log_error "Some tools are missing. Please install them manually."
        exit 1
    fi

    log_ok "All tools verified."
}

# ─── Create Build Directories ─────────────────────────────────────────────────
create_build_dirs() {
    log_step "Creating build directories"

    BUILD_DIRS=(
        "build"
        "build/bootstrap"
        "build/compiler"
        "build/guard"
        "build/pkgman"
        "build/stdlib"
        "dist"
    )

    for DIR in "${BUILD_DIRS[@]}"; do
        mkdir -p "$DIR"
        log_info "Created: $DIR/"
    done

    log_ok "Build directories ready."
}

# ─── Write Version File ───────────────────────────────────────────────────────
write_version_file() {
    log_step "Writing version config"

    cat > VERSION << 'EOF'
CPRIME_VERSION=0.1.0
CPRIME_STAGE=alpha
CPRIME_CODENAME=Backtick
CPRIME_BUILD_DATE=$(date +%Y-%m-%d)
EOF

    log_ok "VERSION file written."
}

# ─── Write environment.sh ─────────────────────────────────────────────────────
write_env_file() {
    log_step "Writing environment configuration"

    cat > scripts/environment.sh << 'EOF'
#!/usr/bin/env bash
# C-Prime build environment variables — source this file before building
# Usage: source scripts/environment.sh

export CPRIME_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export CPRIME_VERSION="0.1.0-alpha"
export CPRIME_BUILD_DIR="$CPRIME_ROOT/build"
export CPRIME_DIST_DIR="$CPRIME_ROOT/dist"
export CPRIME_STDLIB_DIR="$CPRIME_ROOT/stdlib"
export CPRIME_BOOTSTRAP="$CPRIME_BUILD_DIR/bootstrap/cpc-bootstrap"

# Compiler flags for bootstrap C code
export CPRIME_CFLAGS="-O2 -std=c17 -Wall -Wextra -Wpedantic"
export CPRIME_LDFLAGS="-lssl -lcrypto -lcurl -ljson-c -lreadline"

# Paths
export PATH="$CPRIME_BUILD_DIR/bin:$PATH"

echo "[C-Prime] Environment loaded. Root: $CPRIME_ROOT"
EOF

    chmod +x scripts/environment.sh
    log_ok "scripts/environment.sh written."
}

# ─── Git Init ─────────────────────────────────────────────────────────────────
init_git() {
    log_step "Initializing git repository"

    if [ -d ".git" ]; then
        log_info "Git repository already initialized."
    else
        git init
        git add .
        git commit -m "chore: initial project structure — Phase 1"
        log_ok "Git repository initialized with first commit."
    fi
}

# ─── Summary ─────────────────────────────────────────────────────────────────
print_summary() {
    echo ""
    echo -e "${GREEN}${BOLD}╔══════════════════════════════════════════╗"
    echo -e "║     C-Prime Phase 1 Setup Complete!      ║"
    echo -e "╚══════════════════════════════════════════╝${RESET}"
    echo ""
    echo -e "${CYAN}Next steps:${RESET}"
    echo "  Phase 2 → Write the bootstrap compiler in C"
    echo "            cd bootstrap && make"
    echo ""
    echo "  Source the environment:"
    echo "            source scripts/environment.sh"
    echo ""
}

# ─── Main ─────────────────────────────────────────────────────────────────────
main() {
    print_banner
    check_root
    check_distro
    install_apt_packages
    setup_nodejs
    verify_tools
    create_build_dirs
    write_version_file
    write_env_file
    init_git
    print_summary
}

main "$@"
