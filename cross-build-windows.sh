#!/usr/bin/env bash
# ── Cross-compile Particle Playground for Windows (x86_64) from Linux ────────
# Prerequisites: mingw-w64, Vulkan SDK (for glslc + headers)
#   sudo apt install mingw-w64 g++-mingw-w64-x86-64
#   source ~/vulkan/1.4.341.1/setup-env.sh
#
# Usage:
#   ./cross-build-windows.sh          # build
#   ./cross-build-windows.sh package  # build + create distributable zip
#   ./cross-build-windows.sh clean    # wipe build directory

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build-win64"
VULKAN_SDK="${VULKAN_SDK:-$HOME/vulkan/1.4.341.1/x86_64}"
TOOLCHAIN="${SCRIPT_DIR}/cmake/mingw-w64-toolchain.cmake"
VULKAN_STUB_DIR="${BUILD_DIR}/vulkan-stub"
DIST_DIR="${SCRIPT_DIR}/dist-win64"

# ── Colors ───────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'
info()  { echo -e "${CYAN}[info]${NC}  $*"; }
ok()    { echo -e "${GREEN}[ok]${NC}    $*"; }
warn()  { echo -e "${YELLOW}[warn]${NC}  $*"; }
die()   { echo -e "${RED}[error]${NC} $*" >&2; exit 1; }

# ── Clean ────────────────────────────────────────────────────────────────────
if [[ "${1:-}" == "clean" ]]; then
    info "Removing ${BUILD_DIR} and ${DIST_DIR}"
    rm -rf "${BUILD_DIR}" "${DIST_DIR}"
    ok "Clean done"
    exit 0
fi

# ── Preflight checks ────────────────────────────────────────────────────────
command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1 \
    || die "mingw-w64 not found. Install: sudo apt install mingw-w64 g++-mingw-w64-x86-64"
command -v cmake >/dev/null 2>&1 \
    || die "cmake not found"
[[ -d "${VULKAN_SDK}/include/vulkan" ]] \
    || die "Vulkan SDK not found at ${VULKAN_SDK}. Set VULKAN_SDK or source setup-env.sh"
[[ -f "${TOOLCHAIN}" ]] \
    || die "Toolchain file not found: ${TOOLCHAIN}"

GLSLC="${VULKAN_SDK}/bin/glslc"
[[ -x "${GLSLC}" ]] || GLSLC="$(command -v glslc 2>/dev/null || true)"
[[ -x "${GLSLC}" ]] || die "glslc not found in Vulkan SDK or PATH"

ok "MinGW-w64: $(x86_64-w64-mingw32-g++ --version | head -1)"
ok "Vulkan SDK: ${VULKAN_SDK}"
ok "glslc: ${GLSLC}"

# ── Generate Vulkan stub (headers + import lib) for MinGW ────────────────────
# The Linux Vulkan SDK headers are platform-independent, but including them via
# the SDK path can leak host /usr/include headers into the cross-compilation.
# Solution: copy headers to an isolated directory and generate a MinGW import lib.

generate_vulkan_stub() {
    info "Setting up Vulkan headers + import library for MinGW..."
    rm -rf "${VULKAN_STUB_DIR}"
    mkdir -p "${VULKAN_STUB_DIR}/include" "${VULKAN_STUB_DIR}/lib"

    # Copy Vulkan headers to isolated directory (no symlinks — avoids host path leaks)
    cp -r "${VULKAN_SDK}/include/vulkan" "${VULKAN_STUB_DIR}/include/vulkan"
    if [[ -d "${VULKAN_SDK}/include/vk_video" ]]; then
        cp -r "${VULKAN_SDK}/include/vk_video" "${VULKAN_STUB_DIR}/include/vk_video"
    fi

    # Generate .def from Vulkan headers — extract all vk* function declarations
    local DEF_FILE="${VULKAN_STUB_DIR}/vulkan-1.def"
    {
        echo "LIBRARY vulkan-1.dll"
        echo "EXPORTS"
        grep -rh 'VKAPI_ATTR.*VKAPI_CALL\s\+vk[A-Z]' "${VULKAN_STUB_DIR}/include/vulkan/"*.h \
            | sed -n 's/.*VKAPI_CALL\s\+\(vk[A-Za-z0-9_]*\).*/    \1/p' \
            | sort -u
    } > "${DEF_FILE}"

    local EXPORT_COUNT
    EXPORT_COUNT=$(grep -c '^\s\+vk' "${DEF_FILE}")
    info "  Found ${EXPORT_COUNT} Vulkan API exports"

    # Create MinGW import library (.a) from .def
    x86_64-w64-mingw32-dlltool \
        --def "${DEF_FILE}" \
        --dllname vulkan-1.dll \
        --output-lib "${VULKAN_STUB_DIR}/lib/libvulkan-1.a"

    ok "Vulkan stub: ${VULKAN_STUB_DIR}"
}

if [[ ! -f "${VULKAN_STUB_DIR}/lib/libvulkan-1.a" ]]; then
    generate_vulkan_stub
else
    ok "Vulkan stub already exists (use 'clean' to regenerate)"
fi

# ── Configure ────────────────────────────────────────────────────────────────
info "Configuring CMake (PORTABLE_BUILD=ON — shaders + icons embedded in exe)..."
cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DPORTABLE_BUILD=ON \
    -DCMAKE_FIND_ROOT_PATH="${VULKAN_STUB_DIR}" \
    -DVulkan_INCLUDE_DIR="${VULKAN_STUB_DIR}/include" \
    -DVulkan_LIBRARY="${VULKAN_STUB_DIR}/lib/libvulkan-1.a" \
    -DVulkan_GLSLC_EXECUTABLE="${GLSLC}" \
    2>&1 | tail -20

ok "CMake configured"

# ── Build ────────────────────────────────────────────────────────────────────
info "Building ($(nproc) threads)..."
cmake --build "${BUILD_DIR}" -j"$(nproc)" 2>&1

ok "Build complete: ${BUILD_DIR}/particle_physics.exe"

# ── Package ──────────────────────────────────────────────────────────────────
if [[ "${1:-}" == "package" ]]; then
    info "Packaging portable distributable..."
    rm -rf "${DIST_DIR}"
    mkdir -p "${DIST_DIR}"

    # Executable (shaders + icons are embedded)
    cp "${BUILD_DIR}/particle_physics.exe" "${DIST_DIR}/"

    # Optional: bundle background music if present
    if [[ -f "${SCRIPT_DIR}/assets/sound.mp3" ]]; then
        mkdir -p "${DIST_DIR}/assets"
        cp "${SCRIPT_DIR}/assets/sound.mp3" "${DIST_DIR}/assets/"
        info "  Bundled: assets/sound.mp3 (optional — music)"
    fi

    # MinGW runtime DLLs that weren't statically linked
    for dll in libwinpthread-1.dll; do
        DLL_PATH="$(x86_64-w64-mingw32-g++ -print-file-name="${dll}" 2>/dev/null)"
        if [[ -f "${DLL_PATH}" && "${DLL_PATH}" != "${dll}" ]]; then
            cp "${DLL_PATH}" "${DIST_DIR}/"
            info "  Bundled: ${dll}"
        fi
    done

    # Create zip
    ZIP_NAME="ParticlePlayground-win64.zip"
    (cd "${DIST_DIR}/.." && zip -r "${ZIP_NAME}" "$(basename "${DIST_DIR}")")
    ok "Package: $(dirname "${DIST_DIR}")/${ZIP_NAME}"

    echo ""
    echo -e "${GREEN}Distribution contents:${NC}"
    find "${DIST_DIR}" -type f | sort | while read -r f; do
        SIZE=$(du -h "$f" | cut -f1)
        echo "  ${SIZE}  $(realpath --relative-to="${DIST_DIR}" "$f")"
    done
    echo ""
    echo -e "${GREEN}Portable:${NC} Shaders and icons are embedded in the exe."
    echo -e "${GREEN}Optional:${NC} Place assets/sound.mp3 next to exe for background music."
    echo -e "${YELLOW}Note: Users need Vulkan GPU drivers installed (vulkan-1.dll ships with GPU drivers).${NC}"
fi

echo ""
ok "Done! Windows binary: ${BUILD_DIR}/particle_physics.exe"
