#!/usr/bin/env bash
#
# install-toolchain.sh — install a pinned cross-compiler toolchain
#
# Usage:
#   ./tools/install-toolchain.sh aarch64       # ARM AArch64 (Pi 4/5)
#   ./tools/install-toolchain.sh riscv64       # RISC-V RV64 (SG2002 / virt)
#   ./tools/install-toolchain.sh --check-only  # verify existing install
#
# What it does:
#   1. Checks if the toolchain is already installed / on PATH
#   2. If not, downloads a pinned version to ~/.local/cross/<arch>/
#   3. Verifies the compiler works
#
# Pinned versions (intentionally fixed — CI and collaborators get identical
# toolchains, avoiding "it builds on my machine" drift):
#
#   AArch64: ARM GNU Toolchain 13.3.rel1
#     https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
#
#   RISC-V:  upstream GCC 14.2.0 (SiFive pre-built)
#     https://github.com/riscv-collab/riscv-gnu-toolchain/releases
#

set -euo pipefail

# ── Configuration ────────────────────────────────────────────────────────
INSTALL_ROOT="${CROSS_PATH:-$HOME/.local/cross}"

# Toolchain metadata lookup functions (avoid bash 4+ associative arrays
# for macOS compatibility — macOS ships bash 3.2 by default).
tc_version() {
    case "$1" in
        aarch64) echo "13.3.rel1" ;;
        riscv64) echo "2025.01.20" ;;
        *) echo "" ;;
    esac
}
tc_url() {
    case "$1" in
        aarch64) echo "https://developer.arm.com/-/media/Files/downloads/gnu/$(tc_version aarch64)/binrel/arm-gnu-toolchain-$(tc_version aarch64)-x86_64-aarch64-none-elf.tar.xz" ;;
        riscv64) echo "https://github.com/riscv-collab/riscv-gnu-toolchain/releases/download/$(tc_version riscv64)/riscv64-elf-ubuntu-22.04-gcc-nightly-$(tc_version riscv64)-nightly.tar.gz" ;;
        *) echo "" ;;
    esac
}
tc_sha256() {
    case "$1" in
        aarch64) echo "31bbeec581f9ef6c5efdf06c7e1263a7cf7a7cee8c34fb2b54f676e9d74c1589" ;;
        riscv64) echo "" ;;
        *) echo "" ;;
    esac
}
tc_subdir() {
    case "$1" in
        aarch64) echo "arm-gnu-toolchain-$(tc_version aarch64)-x86_64-aarch64-none-elf" ;;
        riscv64) echo "riscv" ;;
        *) echo "" ;;
    esac
}

# ── Help ─────────────────────────────────────────────────────────────────
usage() {
    cat <<EOF
Usage: $0 [aarch64|riscv64|--check-only]
       $0 aarch64              Install AArch64 cross-compiler
       $0 riscv64              Install RISC-V cross-compiler
       $0 --check-only aarch64 Only verify, don't install

Installs to: $INSTALL_ROOT/<arch>/
Override:    CROSS_PATH=/other/path $0 aarch64
EOF
    exit 0
}

# ── Check if toolchain is already usable ─────────────────────────────────
# Returns 0 if the compiler is available (via PATH or INSTALL_ROOT) and
# can compile a trivial C program.
check_toolchain() {
    local arch="$1"
    local gcc_name
    case "$arch" in
        aarch64) gcc_name="aarch64-none-elf-gcc" ;;
        riscv64) gcc_name="riscv64-unknown-elf-gcc" ;;
        *) echo "ERROR: unknown arch '$arch'"; return 1 ;;
    esac

    # Prefer installed compiler in INSTALL_ROOT; fall back to PATH
    local gcc="${INSTALL_ROOT}/${arch}/bin/${gcc_name}"
    if [[ ! -x "$gcc" ]]; then
        # On macOS / Homebrew the prefix may differ — try common names on PATH
        local found
        found=$(command -v "$gcc_name" 2>/dev/null || true)
        if [[ -z "$found" ]]; then
            # Try alternates
            for alt in aarch64-elf-gcc aarch64-linux-gnu-gcc riscv64-elf-gcc riscv64-linux-gnu-gcc; do
                found=$(command -v "$alt" 2>/dev/null || true)
                if [[ -n "$found" ]]; then
                    gcc="$found"
                    break
                fi
            done
        else
            gcc="$found"
        fi
    fi

    if [[ ! -x "$gcc" ]]; then
        echo "  Toolchain not found on PATH or in $INSTALL_ROOT"
        return 1
    fi

    echo "  Found: $gcc"
    echo "  Version: $("$gcc" --version | head -1)"

    # Smoke-test compilation
    local tmpdir
    tmpdir=$(mktemp -d)
    trap "rm -rf $tmpdir" RETURN
    echo 'int main(void){return 0;}' > "$tmpdir/test.c"
    if "$gcc" -nostdlib -ffreestanding "$tmpdir/test.c" -o "$tmpdir/test.elf" 2>/dev/null; then
        echo "  Compilation test: OK"
        return 0
    else
        echo "  Compilation test: FAILED"
        return 1
    fi
}

# ── Install a toolchain ──────────────────────────────────────────────────
install_toolchain() {
    local arch="$1"
    local dest="${INSTALL_ROOT}/${arch}"
    local url
    local sha256
    local subdir
    url=$(tc_url "$arch")
    sha256=$(tc_sha256 "$arch")
    subdir=$(tc_subdir "$arch")

    if [[ -z "$url" ]]; then
        echo "ERROR: no download URL configured for '$arch'"
        exit 1
    fi

    echo "Installing $arch toolchain to $dest"
    echo "  URL: $url"

    mkdir -p "$INSTALL_ROOT"

    local tarball="/tmp/tc-${arch}.tar.${url##*.}"
    # Remove .gz suffix if present to get just .tar
    if [[ "$tarball" == *.tar.gz ]]; then
        :
    fi

    # Download
    if [[ ! -f "$tarball" ]]; then
        echo "  Downloading..."
        curl -L --progress-bar -o "$tarball" "$url"
    else
        echo "  Using cached tarball: $tarball"
    fi

    # Verify checksum (best-effort if we have it)
    if [[ -n "$sha256" && "$(uname -s)" == "Linux" ]]; then
        echo "  Verifying sha256..."
        echo "$sha256  $tarball" | sha256sum -c -
    elif [[ -n "$sha256" && "$(uname -s)" == "Darwin" ]]; then
        echo "  Verifying sha256..."
        if command -v shasum >/dev/null 2>&1; then
            echo "$sha256  $tarball" | shasum -a 256 -c -
        else
            echo "  (shasum not found — skipping checksum)"
        fi
    fi

    # Extract
    echo "  Extracting..."
    rm -rf "/tmp/tc-extract-${arch}"
    mkdir -p "/tmp/tc-extract-${arch}"

    case "$tarball" in
        *.tar.xz) tar -xf "$tarball" -C "/tmp/tc-extract-${arch}" ;;
        *.tar.gz) tar -xzf "$tarball" -C "/tmp/tc-extract-${arch}" ;;
        *)        tar -xf "$tarball" -C "/tmp/tc-extract-${arch}" ;;
    esac

    # Move to final location (tarball usually has one top-level dir)
    rm -rf "$dest"
    local extracted
    extracted=$(find "/tmp/tc-extract-${arch}" -mindepth 1 -maxdepth 1 -type d | head -1)
    if [[ -n "$subdir" && -d "/tmp/tc-extract-${arch}/${subdir}" ]]; then
        mv "/tmp/tc-extract-${arch}/${subdir}" "$dest"
    elif [[ -n "$extracted" ]]; then
        mv "$extracted" "$dest"
    else
        mkdir -p "$dest"
        mv "/tmp/tc-extract-${arch}"/* "$dest"/
    fi

    rm -rf "/tmp/tc-extract-${arch}"
    echo "  Installed: $dest"

    # Add to PATH hint
    echo ""
    echo "  Add to your PATH:"
    echo "    export PATH=\"${dest}/bin:\$PATH\""
}

# ══════════════════════════════════════════════════════════════════════════
# Main
# ══════════════════════════════════════════════════════════════════════════

if [[ $# -eq 0 || "$1" == "-h" || "$1" == "--help" ]]; then
    usage
fi

if [[ "$1" == "--check-only" ]]; then
    arch="${2:-}"
    if [[ -z "$arch" ]]; then
        echo "ERROR: --check-only requires an architecture argument"
        exit 1
    fi
    echo "=== Checking $arch toolchain ==="
    if check_toolchain "$arch"; then
        echo "Toolchain check: PASS"
        exit 0
    else
        echo "Toolchain check: FAIL"
        exit 1
    fi
fi

arch="$1"
case "$arch" in
    aarch64|riscv64) ;;
    *) echo "ERROR: unknown arch '$arch'.  Supported: aarch64, riscv64"; exit 1 ;;
esac

echo "=== $arch cross-compiler toolchain ==="
echo ""

# Check if already installed
if check_toolchain "$arch"; then
    echo ""
    echo "Toolchain is already available — nothing to install."
    echo "Run with --force to re-install:  rm -rf ${INSTALL_ROOT}/${arch}"
    exit 0
fi

# Not found — install
echo ""
install_toolchain "$arch"

# Verify
echo ""
echo "=== Verifying ==="
export PATH="${INSTALL_ROOT}/${arch}/bin:$PATH"
if check_toolchain "$arch"; then
    echo ""
    echo "Installation successful!"
else
    echo ""
    echo "ERROR: Installation failed — compiler not usable after install"
    exit 1
fi
