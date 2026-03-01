#!/usr/bin/env bash
# ── Particle Playground — Unified Build Script ────────────────────────────────
#
# Usage:
#   ./build.sh [command] [options...]
#
# Commands:
#   linux              Build Linux x64 (default)
#   win64              Cross-compile Windows x64 via MinGW
#   all                Build both targets
#   package [TARGET]   Build + create distributable zip (default: all)
#   clean   [TARGET]   Wipe build directories (default: all)
#   tools              Build standalone generator tools (textures, sound effects)
#   help               Show this message
#
# Options (combine freely):
#   --steam            Link Steam SDK (auto-detects steam/sdk/)
#   --debug            Debug build (-O0 -g, assertions enabled)
#   --release          Release build (default, -O3)
#   --reldbg           Release with debug info (-O2 -g)
#   --o2               Use -O2 instead of default optimization
#   --o3               Use -O3 (default for release)
#   --lto              Link-Time Optimization (smaller + faster binary)
#   --native           Tune for current CPU (-march=native)
#   --sanitize         ASan + UBSan (implies --debug)
#   --no-steam         Explicitly disable Steam even if SDK exists
#
# Examples:
#   ./build.sh                          # Linux Release -O3
#   ./build.sh linux --steam            # Linux Release with Steam
#   ./build.sh linux --debug --sanitize # Linux Debug with sanitizers
#   ./build.sh linux --lto --native     # Linux Release, LTO + native tuning
#   ./build.sh win64 --steam            # Win64 Release with Steam
#   ./build.sh all --steam --lto        # Both targets, Steam + LTO
#   ./build.sh tools                    # Build gen_sfx + gen_textures
#   ./build.sh package win64 --steam    # Build + package Win64 with Steam
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

# ── Defaults ─────────────────────────────────────────────────────────────────
BUILD_TYPE="Release"
OPT_LEVEL=""           # empty = use CMake default for build type
USE_LTO=0
USE_NATIVE=0
USE_SANITIZE=0
USE_STEAM=""           # empty = auto-detect, "on" = force, "off" = disable
EXTRA_CXX_FLAGS=""
EXTRA_LINKER_FLAGS=""

# ── Colors ───────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'
info()  { echo -e "${CYAN}[info]${NC}  $*"; }
ok()    { echo -e "${GREEN}[ok]${NC}    $*"; }
warn()  { echo -e "${YELLOW}[warn]${NC}  $*"; }
die()   { echo -e "${RED}[error]${NC} $*" >&2; exit 1; }

# ── Parse arguments ──────────────────────────────────────────────────────────

source ~/vulkan/1.4.341.1/setup-env.sh


COMMAND=""
TARGET=""
POSITIONALS=()

for arg in "$@"; do
    case "${arg}" in
        --steam)      USE_STEAM="on" ;;
        --no-steam)   USE_STEAM="off" ;;
        --debug)      BUILD_TYPE="Debug" ;;
        --release)    BUILD_TYPE="Release" ;;
        --reldbg)     BUILD_TYPE="RelWithDebInfo" ;;
        --o2)         OPT_LEVEL="-O2" ;;
        --o3)         OPT_LEVEL="-O3" ;;
        --lto)        USE_LTO=1 ;;
        --native)     USE_NATIVE=1 ;;
        --sanitize)   USE_SANITIZE=1; BUILD_TYPE="Debug" ;;
        -h|--help)    COMMAND="help" ;;
        -*)           die "Unknown option: ${arg}. Run '$0 help' for usage." ;;
        *)            POSITIONALS+=("${arg}") ;;
    esac
done

# First positional = command, second = target (for package/clean)
COMMAND="${COMMAND:-${POSITIONALS[0]:-linux}}"
TARGET="${POSITIONALS[1]:-all}"

# ── Build extra flags ────────────────────────────────────────────────────────

if [[ -n "${OPT_LEVEL}" ]]; then
    EXTRA_CXX_FLAGS="${EXTRA_CXX_FLAGS} ${OPT_LEVEL}"
fi

if (( USE_NATIVE )); then
    EXTRA_CXX_FLAGS="${EXTRA_CXX_FLAGS} -march=native -mtune=native"
fi

if (( USE_SANITIZE )); then
    EXTRA_CXX_FLAGS="${EXTRA_CXX_FLAGS} -fsanitize=address,undefined -fno-omit-frame-pointer"
    EXTRA_LINKER_FLAGS="${EXTRA_LINKER_FLAGS} -fsanitize=address,undefined"
fi

# ── Steam SDK resolution ────────────────────────────────────────────────────

STEAM_SDK_DIR=""

resolve_steam() {
    if [[ "${USE_STEAM}" == "off" ]]; then
        return
    fi

    if [[ -d "${SCRIPT_DIR}/steam/sdk" ]]; then
        STEAM_SDK_DIR="${SCRIPT_DIR}/steam/sdk"
    elif [[ -n "${STEAMWORKS_SDK_DIR:-}" ]]; then
        STEAM_SDK_DIR="${STEAMWORKS_SDK_DIR}"
    fi

    if [[ -n "${STEAM_SDK_DIR}" ]]; then
        if [[ "${USE_STEAM}" == "on" ]] || [[ "${USE_STEAM}" == "" ]]; then
            info "Steam SDK: ${STEAM_SDK_DIR}"
        else
            STEAM_SDK_DIR=""
        fi
    elif [[ "${USE_STEAM}" == "on" ]]; then
        die "Steam SDK not found. Place it at steam/sdk/ or set STEAMWORKS_SDK_DIR."
    fi
}

# ── CMake argument builder ───────────────────────────────────────────────────

build_cmake_args() {
    local build_dir="$1"

    CMAKE_ARGS=()
    CMAKE_ARGS+=("-S" "${SCRIPT_DIR}" "-B" "${build_dir}")
    CMAKE_ARGS+=("-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")

    if (( USE_LTO )); then
        CMAKE_ARGS+=("-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON")
    fi

    if [[ -n "${EXTRA_CXX_FLAGS}" ]]; then
        CMAKE_ARGS+=("-DCMAKE_CXX_FLAGS_INIT=${EXTRA_CXX_FLAGS}")
    fi

    if [[ -n "${EXTRA_LINKER_FLAGS}" ]]; then
        CMAKE_ARGS+=("-DCMAKE_EXE_LINKER_FLAGS_INIT=${EXTRA_LINKER_FLAGS}")
    fi

    if [[ -n "${STEAM_SDK_DIR}" ]]; then
        CMAKE_ARGS+=("-DSTEAMWORKS_SDK_DIR=${STEAM_SDK_DIR}")
    fi
}

# ── Print build configuration ───────────────────────────────────────────────

print_config() {
    echo -e "${BOLD}Build configuration:${NC}"
    echo "  Type:       ${BUILD_TYPE}"
    [[ -n "${OPT_LEVEL}" ]] && echo "  Opt level:  ${OPT_LEVEL}" || echo "  Opt level:  default (${BUILD_TYPE})"
    (( USE_LTO ))      && echo "  LTO:        enabled"
    (( USE_NATIVE ))   && echo "  Native:     -march=native"
    (( USE_SANITIZE )) && echo "  Sanitizers: ASan + UBSan"
    if [[ -n "${STEAM_SDK_DIR}" ]]; then
        echo "  Steam:      enabled"
    elif [[ "${USE_STEAM}" == "off" ]]; then
        echo "  Steam:      disabled"
    fi
    echo ""
}

# ── Preflight ────────────────────────────────────────────────────────────────

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

# ── Vulkan stub for MinGW cross-compilation ──────────────────────────────────

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

# ── Build targets ────────────────────────────────────────────────────────────

build_linux() {
    check_linux
    resolve_steam
    echo ""
    echo -e "${BOLD}── Linux x64 ──────────────────────────────────────────────${NC}"
    print_config

    build_cmake_args "${BUILD_LINUX}"

    info "Configuring CMake..."
    cmake "${CMAKE_ARGS[@]}" 2>&1 | tail -20
    ok "CMake configured"

    info "Building ($(nproc) threads)..."
    cmake --build "${BUILD_LINUX}" -j"$(nproc)" 2>&1
    ok "Linux build: ${BUILD_LINUX}/particle_physics"
}

build_win64() {
    check_win64
    resolve_steam
    local GLSLC
    GLSLC="$(find_glslc)"

    echo ""
    echo -e "${BOLD}── Windows x64 (MinGW cross-compile) ──────────────────────${NC}"
    print_config

    ok "MinGW-w64: $(x86_64-w64-mingw32-g++ --version | head -1)"
    ok "Vulkan SDK: ${VULKAN_SDK}"
    ok "glslc: ${GLSLC}"

    if [[ ! -f "${VULKAN_STUB_DIR}/lib/libvulkan-1.a" ]]; then
        generate_vulkan_stub
    else
        ok "Vulkan stub already exists (use 'clean' to regenerate)"
    fi

    build_cmake_args "${BUILD_WIN64}"

    info "Configuring CMake (PORTABLE_BUILD=ON)..."
    cmake "${CMAKE_ARGS[@]}" \
        -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" \
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

# ── Tools ────────────────────────────────────────────────────────────────────

build_tools() {
    echo ""
    echo -e "${BOLD}── Generator Tools ────────────────────────────────────────${NC}"
    mkdir -p "${BUILD_LINUX}"

    local tool_cxx="${OPT_LEVEL:--O2}"

    if [[ -f "${SCRIPT_DIR}/tools/gen_sound_effects.cpp" ]]; then
        info "Building gen_sfx..."
        g++ -std=c++17 ${tool_cxx} \
            -o "${BUILD_LINUX}/gen_sfx" \
            "${SCRIPT_DIR}/tools/gen_sound_effects.cpp" -lm
        ok "gen_sfx -> ${BUILD_LINUX}/gen_sfx"
    fi

    if [[ -f "${SCRIPT_DIR}/tools/gen_particle_textures.cpp" ]]; then
        info "Building gen_textures..."
        g++ -std=c++17 ${tool_cxx} \
            -o "${BUILD_LINUX}/gen_textures" \
            "${SCRIPT_DIR}/tools/gen_particle_textures.cpp" -lm
        ok "gen_textures -> ${BUILD_LINUX}/gen_textures"
    fi

    if [[ -f "${SCRIPT_DIR}/tools/ppmol/ppmol_gen.cpp" ]]; then
        info "Building ppmol_gen..."
        g++ -std=c++17 ${tool_cxx} \
            -I"${SCRIPT_DIR}/src" \
            -o "${BUILD_LINUX}/ppmol_gen" \
            "${SCRIPT_DIR}/tools/ppmol/ppmol_gen.cpp" -lm
        ok "ppmol_gen -> ${BUILD_LINUX}/ppmol_gen"
    fi

    echo ""
    info "Run generators:"
    [[ -f "${BUILD_LINUX}/gen_sfx" ]]      && echo "  ${BUILD_LINUX}/gen_sfx         -> assets/sfx/*.wav"
    [[ -f "${BUILD_LINUX}/gen_textures" ]]  && echo "  ${BUILD_LINUX}/gen_textures    -> assets/particles/*.png"
    [[ -f "${BUILD_LINUX}/ppmol_gen" ]]    && echo "  ${BUILD_LINUX}/ppmol_gen       -> .ppmol molecule files"
}

# ── Package ──────────────────────────────────────────────────────────────────

package_win64() {
    info "Packaging Windows distributable..."
    rm -rf "${DIST_DIR}"
    mkdir -p "${DIST_DIR}"

    cp "${BUILD_WIN64}/particle_physics.exe" "${DIST_DIR}/"

    # Bundle assets
    for asset_dir in assets/sfx assets/particles; do
        if [[ -d "${SCRIPT_DIR}/${asset_dir}" ]]; then
            mkdir -p "${DIST_DIR}/${asset_dir}"
            cp -r "${SCRIPT_DIR}/${asset_dir}/." "${DIST_DIR}/${asset_dir}/"
            info "  Bundled: ${asset_dir}/"
        fi
    done
    if [[ -f "${SCRIPT_DIR}/assets/sound.mp3" ]]; then
        mkdir -p "${DIST_DIR}/assets"
        cp "${SCRIPT_DIR}/assets/sound.mp3" "${DIST_DIR}/assets/"
        info "  Bundled: assets/sound.mp3"
    fi

    # Steam runtime library
    if [[ -f "${SCRIPT_DIR}/steam/sdk/redistributable_bin/win64/steam_api64.dll" ]]; then
        cp "${SCRIPT_DIR}/steam/sdk/redistributable_bin/win64/steam_api64.dll" "${DIST_DIR}/"
        info "  Bundled: steam_api64.dll"
    fi
    if [[ -f "${SCRIPT_DIR}/steam_appid.txt" ]]; then
        cp "${SCRIPT_DIR}/steam_appid.txt" "${DIST_DIR}/"
        info "  Bundled: steam_appid.txt"
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

# ── Clean ────────────────────────────────────────────────────────────────────

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

# ── Usage ────────────────────────────────────────────────────────────────────

usage() {
    echo -e "${BOLD}Particle Playground Build Script${NC}"
    echo ""
    echo "Usage: $0 [command] [options...]"
    echo ""
    echo -e "${BOLD}Commands:${NC}"
    echo "  linux              Build Linux x64 (default)"
    echo "  win64              Cross-compile Windows x64 via MinGW"
    echo "  all                Build both targets"
    echo "  package [TARGET]   Build + package distributable (default: all)"
    echo "  clean [TARGET]     Remove build directories (default: all)"
    echo "  tools              Build standalone generator tools"
    echo "  help               Show this message"
    echo ""
    echo -e "${BOLD}Build type:${NC}"
    echo "  --debug            Debug build (-O0 -g, assertions on)"
    echo "  --release          Release build (default)"
    echo "  --reldbg           Release with debug info (-O2 -g, NDEBUG)"
    echo ""
    echo -e "${BOLD}Optimization:${NC}"
    echo "  --o2               Use -O2 (good balance of speed + compile time)"
    echo "  --o3               Use -O3 (aggressive, default for --release)"
    echo "  --lto              Link-Time Optimization (slower build, faster binary)"
    echo "  --native           -march=native (tune for current CPU, not portable)"
    echo ""
    echo -e "${BOLD}Integration:${NC}"
    echo "  --steam            Link Steam SDK (auto-detects steam/sdk/)"
    echo "  --no-steam         Disable Steam even if SDK is present"
    echo ""
    echo -e "${BOLD}Debugging:${NC}"
    echo "  --sanitize         Enable ASan + UBSan (implies --debug)"
    echo ""
    echo -e "${BOLD}Examples:${NC}"
    echo "  $0                                # Linux Release -O3"
    echo "  $0 linux --steam                  # Linux Release + Steam"
    echo "  $0 linux --debug --sanitize       # Debug + sanitizers"
    echo "  $0 linux --release --lto --native # Max optimization"
    echo "  $0 win64 --steam                  # Win64 Release + Steam"
    echo "  $0 all --steam --lto              # Both targets, full optimization"
    echo "  $0 tools                          # Build gen_sfx + gen_textures"
    echo "  $0 package win64 --steam          # Build + zip Win64 with Steam"
}

# ── Main ─────────────────────────────────────────────────────────────────────

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
    tools)
        build_tools
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
