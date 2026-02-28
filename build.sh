#!/usr/bin/env bash
# ── Particle Playground — Unified Build Script ────────────────────────────────
#
# Builds Linux x64 (native) and/or Windows x64 (MinGW cross-compilation).
#
# Usage:
#   ./build.sh                  # build Linux (default)
#   ./build.sh linux            # build Linux
#   ./build.sh win64            # cross-compile Windows
#   ./build.sh all              # build both targets
#   ./build.sh package [TARGET] # build + create distributable zip (default: all)
#   ./build.sh clean   [TARGET] # wipe build directories (default: all)
#
# Prerequisites:
#   Linux:  libvulkan-dev vulkan-tools glslang-tools libglfw3-dev libglm-dev cmake g++
#   Win64:  mingw-w64 g++-mingw-w64-x86-64
#   Both:   source ~/vulkan/1.4.341.1/setup-env.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VULKAN_SDK="${VULKAN_SDK:-$HOME/vulkan/1.4.341.1/x86_64}"
TOOLCHAIN="${SCRIPT_DIR}/cmake/mingw-w64-toolchain.cmake"

# Build directories
BUILD_LINUX="${SCRIPT_DIR}/build"
BUILD_WIN64="${SCRIPT_DIR}/build-win64"
DIST_DIR="${SCRIPT_DIR}/dist-win64"
VULKAN_STUB_DIR="${BUILD_WIN64}/vulkan-stub"

# ── Colors ────────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'
info()  { echo -e "${CYAN}[info]${NC}  $*"; }
ok()    { echo -e "${GREEN}[ok]${NC}    $*"; }
warn()  { echo -e "${YELLOW}[warn]${NC}  $*"; }
die()   { echo -e "${RED}[error]${NC} $*" >&2; exit 1; }

# ── Preflight ─────────────────────────────────────────────────────────────────

check_common() {
    command -v cmake >/dev/null 2>&1 || die "cmake not found"
}

check_linux() {
    check_common
    command -v g++ >/dev/null 2>&1 || die "g++ not found. Install: sudo apt install g++"
}

check_win64() {
    check_common
    command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1 \
        || die "mingw-w64 not found. Install: sudo apt install mingw-w64 g++-mingw-w64-x86-64"
    [[ -d "${VULKAN_SDK}/include/vulkan" ]] \
        || die "Vulkan SDK not found at ${VULKAN_SDK}. Set VULKAN_SDK or source setup-env.sh"
    [[ -f "${TOOLCHAIN}" ]] \
        || die "Toolchain file not found: ${TOOLCHAIN}"
}

find_glslc() {
    local glslc="${VULKAN_SDK}/bin/glslc"
    [[ -x "${glslc}" ]] || glslc="$(command -v glslc 2>/dev/null || true)"
    [[ -x "${glslc}" ]] || die "glslc not found in Vulkan SDK or PATH"
    echo "${glslc}"
}

# ── Vulkan stub for MinGW cross-compilation ───────────────────────────────────
# Copies Vulkan headers to an isolated directory and generates a MinGW import
# library so host /usr/include doesn't leak into the cross build.

generate_vulkan_stub() {
    info "Setting up Vulkan headers + import library for MinGW..."
    rm -rf "${VULKAN_STUB_DIR}"
    mkdir -p "${VULKAN_STUB_DIR}/include" "${VULKAN_STUB_DIR}/lib"

    cp -r "${VULKAN_SDK}/include/vulkan" "${VULKAN_STUB_DIR}/include/vulkan"
    if [[ -d "${VULKAN_SDK}/include/vk_video" ]]; then
        cp -r "${VULKAN_SDK}/include/vk_video" "${VULKAN_STUB_DIR}/include/vk_video"
    fi

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

    x86_64-w64-mingw32-dlltool \
        --def "${DEF_FILE}" \
        --dllname vulkan-1.dll \
        --output-lib "${VULKAN_STUB_DIR}/lib/libvulkan-1.a"

    ok "Vulkan stub: ${VULKAN_STUB_DIR}"
}

# ── Build targets ─────────────────────────────────────────────────────────────

build_linux() {
    check_linux
    echo ""
    echo -e "${BOLD}── Linux x64 ──────────────────────────────────────────────${NC}"

    info "Configuring CMake..."
    cmake -S "${SCRIPT_DIR}" -B "${BUILD_LINUX}" \
        -DCMAKE_BUILD_TYPE=Release \
        2>&1 | tail -20
    ok "CMake configured"

    info "Building ($(nproc) threads)..."
    cmake --build "${BUILD_LINUX}" -j"$(nproc)" 2>&1
    ok "Linux build: ${BUILD_LINUX}/particle_physics"
}

build_win64() {
    check_win64
    local GLSLC
    GLSLC="$(find_glslc)"

    echo ""
    echo -e "${BOLD}── Windows x64 (MinGW cross-compile) ──────────────────────${NC}"

    ok "MinGW-w64: $(x86_64-w64-mingw32-g++ --version | head -1)"
    ok "Vulkan SDK: ${VULKAN_SDK}"
    ok "glslc: ${GLSLC}"

    if [[ ! -f "${VULKAN_STUB_DIR}/lib/libvulkan-1.a" ]]; then
        generate_vulkan_stub
    else
        ok "Vulkan stub already exists (use 'clean' to regenerate)"
    fi

    info "Configuring CMake (PORTABLE_BUILD=ON)..."
    cmake -S "${SCRIPT_DIR}" -B "${BUILD_WIN64}" \
        -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DPORTABLE_BUILD=ON \
        -DCMAKE_FIND_ROOT_PATH="${VULKAN_STUB_DIR}" \
        -DVulkan_INCLUDE_DIR="${VULKAN_STUB_DIR}/include" \
        -DVulkan_LIBRARY="${VULKAN_STUB_DIR}/lib/libvulkan-1.a" \
        -DVulkan_GLSLC_EXECUTABLE="${GLSLC}" \
        2>&1 | tail -20
    ok "CMake configured"

    info "Building ($(nproc) threads)..."
    cmake --build "${BUILD_WIN64}" -j"$(nproc)" 2>&1
    ok "Win64 build: ${BUILD_WIN64}/particle_physics.exe"
}

# ── Package ───────────────────────────────────────────────────────────────────

package_win64() {
    info "Packaging Windows distributable..."
    rm -rf "${DIST_DIR}"
    mkdir -p "${DIST_DIR}"

    cp "${BUILD_WIN64}/particle_physics.exe" "${DIST_DIR}/"

    if [[ -f "${SCRIPT_DIR}/assets/sound.mp3" ]]; then
        mkdir -p "${DIST_DIR}/assets"
        cp "${SCRIPT_DIR}/assets/sound.mp3" "${DIST_DIR}/assets/"
        info "  Bundled: assets/sound.mp3"
    fi

    for dll in libwinpthread-1.dll; do
        DLL_PATH="$(x86_64-w64-mingw32-g++ -print-file-name="${dll}" 2>/dev/null)"
        if [[ -f "${DLL_PATH}" && "${DLL_PATH}" != "${dll}" ]]; then
            cp "${DLL_PATH}" "${DIST_DIR}/"
            info "  Bundled: ${dll}"
        fi
    done

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
    echo -e "${YELLOW}Note:${NC} Users need Vulkan GPU drivers installed (vulkan-1.dll ships with GPU drivers)."
}

# ── Clean ─────────────────────────────────────────────────────────────────────

clean_target() {
    local target="${1:-all}"
    case "${target}" in
        linux)
            info "Removing ${BUILD_LINUX}"
            rm -rf "${BUILD_LINUX}"
            ;;
        win64)
            info "Removing ${BUILD_WIN64} and ${DIST_DIR}"
            rm -rf "${BUILD_WIN64}" "${DIST_DIR}"
            ;;
        all)
            info "Removing ${BUILD_LINUX}, ${BUILD_WIN64}, and ${DIST_DIR}"
            rm -rf "${BUILD_LINUX}" "${BUILD_WIN64}" "${DIST_DIR}"
            ;;
        *)
            die "Unknown clean target: ${target}. Use: linux, win64, all"
            ;;
    esac
    ok "Clean done"
}

# ── Usage ─────────────────────────────────────────────────────────────────────

usage() {
    echo "Usage: $0 [command] [target]"
    echo ""
    echo "Commands:"
    echo "  linux            Build Linux x64 (default)"
    echo "  win64            Cross-compile Windows x64 via MinGW"
    echo "  all              Build both targets"
    echo "  package [TARGET] Build + package distributable (default: all)"
    echo "  clean [TARGET]   Remove build directories (default: all)"
    echo "  help             Show this message"
    echo ""
    echo "Targets for package/clean: linux, win64, all"
}

# ── Main ──────────────────────────────────────────────────────────────────────

COMMAND="${1:-linux}"
TARGET="${2:-all}"

case "${COMMAND}" in
    linux)
        build_linux
        ;;
    win64)
        build_win64
        ;;
    all)
        build_linux
        build_win64
        ;;
    package)
        case "${TARGET}" in
            linux)
                build_linux
                warn "No packaging step for Linux (run directly from build/)"
                ;;
            win64)
                build_win64
                package_win64
                ;;
            all)
                build_linux
                build_win64
                package_win64
                ;;
            *)
                die "Unknown package target: ${TARGET}"
                ;;
        esac
        ;;
    clean)
        clean_target "${TARGET}"
        exit 0
        ;;
    help|-h|--help)
        usage
        exit 0
        ;;
    *)
        die "Unknown command: ${COMMAND}. Run '$0 help' for usage."
        ;;
esac

echo ""
ok "Done!"
