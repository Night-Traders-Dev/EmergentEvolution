cmake_minimum_required(VERSION 3.20)
project(particle_playground VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Dependencies ──────────────────────────────────────────────────────────────

include(FetchContent)

find_package(Vulkan REQUIRED)

# GLFW — try system/vcpkg first, fall back to FetchContent
find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
    message(STATUS "glfw3 not found via find_package — fetching with FetchContent")
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
    )
    FetchContent_MakeAvailable(glfw)
endif()

# GLM — try system/vcpkg first, fall back to FetchContent
# When cross-compiling, skip system GLM (its include paths pollute the build)
if(NOT CMAKE_CROSSCOMPILING)
    find_package(glm QUIET)
endif()
if(NOT glm_FOUND)
    message(STATUS "glm not found or cross-compiling — fetching with FetchContent")
    FetchContent_Declare(glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        1.0.1
    )
    FetchContent_MakeAvailable(glm)
endif()

# OpenMP — optional, skip when cross-compiling (avoids host libgomp dependency)
if(NOT CMAKE_CROSSCOMPILING)
    find_package(OpenMP QUIET)
endif()

# Steamworks SDK — optional, for Steam achievements and cloud saves
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
find_package(Steamworks QUIET)

# libcurl — optional, for ParticleRepository online features
# Skip when cross-compiling (host libcurl injects Linux headers into the search path)
if(NOT CMAKE_CROSSCOMPILING)
    find_package(CURL QUIET)
endif()

# Threads (required by miniaudio audio engine)
find_package(Threads REQUIRED)

# ImGui via FetchContent (no install needed)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.0
)
FetchContent_MakeAvailable(imgui)

# ═══════════════════════════════════════════════════════════════════════════════
# pp_common — shared infrastructure library
# ═══════════════════════════════════════════════════════════════════════════════

add_library(pp_common STATIC
    # Vulkan abstraction
    src/common/vulkan_context.cpp

    # Minimal ImGui renderer (swap-chain + ImGui, no compute)
    src/common/simple_renderer.cpp

    # Image I/O
    src/common/stb_image_impl.cpp
    src/common/stb_image_write_impl.cpp

    # Audio engine (miniaudio implementation unit)
    src/common/audio.cpp

    # Error dialogs
    src/common/error_dialog.cpp

    # HTTP client (libcurl or WinHTTP backend)
    src/common/http_client.cpp

    # ImGui core
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp

    # ImGui platform/renderer backends
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)

# PUBLIC: propagated to all targets that link pp_common
target_include_directories(pp_common PUBLIC
    src/
    src/third_party/
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(pp_common PUBLIC
    Vulkan::Vulkan
    glfw
    glm::glm
    ${CMAKE_DL_LIBS}       # dlopen for miniaudio backend loading
    Threads::Threads        # pthreads for miniaudio
)

# libm — needed on Linux/MinGW, not MSVC
if(NOT MSVC)
    target_link_libraries(pp_common PUBLIC m)
endif()

# Windows system libs (WASAPI audio, windowing)
if(WIN32)
    target_link_libraries(pp_common PUBLIC ole32 winmm shell32)
endif()

# HTTP backend — libcurl on Linux, WinHTTP on Windows
if(CURL_FOUND)
    target_link_libraries(pp_common PRIVATE CURL::libcurl)
    target_compile_definitions(pp_common PRIVATE HAS_CURL)
    target_compile_definitions(pp_common PUBLIC  HAS_HTTP)
elseif(WIN32)
    target_link_libraries(pp_common PRIVATE winhttp)
    target_compile_definitions(pp_common PRIVATE HAS_WINHTTP)
    target_compile_definitions(pp_common PUBLIC  HAS_HTTP)
endif()

# Version info (major/minor/patch propagated to all targets)
target_compile_definitions(pp_common PUBLIC
    APP_VERSION_MAJOR=${CMAKE_PROJECT_VERSION_MAJOR}
    APP_VERSION_MINOR=${CMAKE_PROJECT_VERSION_MINOR}
    APP_VERSION_PATCH=${CMAKE_PROJECT_VERSION_PATCH}
)

# Windows: prevent <windows.h> macro pollution
target_compile_definitions(pp_common PUBLIC
    $<$<PLATFORM_ID:Windows>:NOMINMAX WIN32_LEAN_AND_MEAN>
    GLM_ENABLE_EXPERIMENTAL
)

# Compiler warnings
target_compile_options(pp_common PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU,Clang>>:-O3>
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:MSVC>>:/O2>
)

# ═══════════════════════════════════════════════════════════════════════════════
# Particle Physics — subatomic particle simulation
# ═══════════════════════════════════════════════════════════════════════════════

add_executable(particle_physics
    src/physics/main.cpp

    # Core — simulation engine, types, particle definitions
    src/physics/core/simulation.cpp
    src/physics/core/phys_particles.cpp

    # Processes — CPU-side physics (nuclear, decay, quantum, etc.)
    src/physics/processes/nuclear.cpp
    src/physics/processes/orbital.cpp
    src/physics/processes/decay.cpp
    src/physics/processes/meson_decays.cpp
    src/physics/processes/quantum.cpp
    src/physics/processes/cp_violation.cpp
    src/physics/processes/spawning.cpp

    # Rendering — Vulkan compute pipeline, renderer, particle buffers
    src/physics/rendering/compute_pipeline.cpp
    src/physics/rendering/renderer.cpp
    src/physics/rendering/particles.cpp
    src/physics/rendering/particle_textures.cpp

    # UI — ImGui interface, panels, cards, dialogs, overlays
    src/physics/ui/interface.cpp
    src/physics/ui/ui_panels.cpp
    src/physics/ui/ui_cards.cpp
    src/physics/ui/ui_lists.cpp
    src/physics/ui/ui_dialogs.cpp
    src/physics/ui/ui_tools.cpp
    src/physics/ui/ui_overlays.cpp
    src/physics/ui/ui_repository.cpp

    # Gameplay — achievements, scenarios, tutorial, cutscenes
    src/physics/gameplay/achievements.cpp
    src/physics/gameplay/scenarios.cpp
    src/physics/gameplay/tutorial.cpp
    src/physics/gameplay/cutscenes.cpp

    # Features — save/load, online repository, Steam integration
    src/physics/features/save_load.cpp
    src/physics/features/repository.cpp
    src/physics/features/steam_integration.cpp
)

target_link_libraries(particle_physics PRIVATE pp_common)
target_compile_definitions(particle_physics PRIVATE APP_VERSION="1.2.0")

# cJSON — needed for repository JSON parsing (when HTTP is available)
if(CURL_FOUND OR WIN32)
    target_sources(particle_physics PRIVATE src/third_party/cjson/cJSON.c)
endif()

# Windows .exe icon + version info
if(WIN32)
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -I ${CMAKE_SOURCE_DIR}")
    target_sources(particle_physics PRIVATE assets/particle_playground.rc)
endif()

# OpenMP — link if available
if(OpenMP_FOUND)
    target_link_libraries(particle_physics PRIVATE OpenMP::OpenMP_CXX)
    target_compile_definitions(particle_physics PRIVATE HAS_OPENMP)
endif()

# Steamworks — link if available
if(Steamworks_FOUND)
    target_link_libraries(particle_physics PRIVATE Steamworks::Steamworks)
    target_compile_definitions(particle_physics PRIVATE HAS_STEAM)
endif()

# Compiler warnings for physics target
target_compile_options(particle_physics PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU,Clang>>:-O3>
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:MSVC>>:/O2>
)

# ═══════════════════════════════════════════════════════════════════════════════
# Launcher — expansion picker
# ═══════════════════════════════════════════════════════════════════════════════

add_executable(pp_launcher
    src/launcher/main.cpp
)
target_link_libraries(pp_launcher PRIVATE pp_common)
target_compile_definitions(pp_launcher PRIVATE APP_VERSION="1.0.0")

# ═══════════════════════════════════════════════════════════════════════════════
# Cosmic Sandbox — celestial body / universe simulation
# ═══════════════════════════════════════════════════════════════════════════════

add_executable(particle_cosmos
    src/cosmos/main.cpp
    src/cosmos/cosmos_app.cpp
    src/cosmos/core/cosmos_body_model.cpp
    src/cosmos/io/cosmos_app_io.cpp
    src/cosmos/ui/cosmos_app_ui.cpp
    src/cosmos/cosmos_gravity_compute.cpp
    src/cosmos/cosmos_nebula_compute.cpp
    src/cosmos/cosmos_raytracer.cpp
)
target_link_libraries(particle_cosmos PRIVATE pp_common)
target_compile_definitions(particle_cosmos PRIVATE APP_VERSION="1.1.0")

# ═══════════════════════════════════════════════════════════════════════════════
# Biochemical Simulator — cells, bacteria, viruses
# ═══════════════════════════════════════════════════════════════════════════════

add_executable(particle_biochem
    src/biochem/main.cpp
    src/biochem/biochem_app.cpp
    src/biochem/biochem_raytracer.cpp
)
target_link_libraries(particle_biochem PRIVATE pp_common)
target_compile_definitions(particle_biochem PRIVATE APP_VERSION="0.3.0")

# ── Shader compilation ────────────────────────────────────────────────────────

# glslc is shipped with the Vulkan SDK; CMake 3.20+ exposes it
if(NOT Vulkan_GLSLC_EXECUTABLE)
    find_program(Vulkan_GLSLC_EXECUTABLE glslc
        HINTS "$ENV{VULKAN_SDK}/bin" "$ENV{VULKAN_SDK}/Bin"
              /usr/bin /usr/local/bin)
endif()
if(NOT Vulkan_GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Install the Vulkan SDK.")
endif()

set(SHADER_SRC_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(SHADER_BIN_DIR ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_BIN_DIR})

# Helper macro — compile into BOTH build dir and source dir
# (runtime loads from source-relative paths like "shaders/physics.spv")
macro(compile_shader INPUT OUTPUT)
    add_custom_command(
        OUTPUT  ${SHADER_BIN_DIR}/${OUTPUT}
        COMMAND ${Vulkan_GLSLC_EXECUTABLE} ${SHADER_SRC_DIR}/${INPUT}
                -o ${SHADER_BIN_DIR}/${OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${SHADER_BIN_DIR}/${OUTPUT} ${SHADER_SRC_DIR}/${OUTPUT}
        DEPENDS ${SHADER_SRC_DIR}/${INPUT}
        COMMENT "Compiling shader: ${INPUT} -> ${OUTPUT}"
        VERBATIM
    )
endmacro()

compile_shader(physics.comp      physics.spv)
compile_shader(fullscreen.vert   fullscreen.vert.spv)
compile_shader(fullscreen.frag   fullscreen.frag.spv)
compile_shader(overlay.vert      overlay.vert.spv)
compile_shader(overlay.frag      overlay.frag.spv)
compile_shader(cosmos_rt.vert    cosmos_rt.vert.spv)
compile_shader(cosmos_rt.frag    cosmos_rt.frag.spv)
compile_shader(cosmos_bh.comp    cosmos_bh.spv)
compile_shader(cosmos_nebula.comp cosmos_nebula.spv)
compile_shader(biochem_rt.vert   biochem_rt.vert.spv)
compile_shader(biochem_rt.frag   biochem_rt.frag.spv)

add_custom_target(shaders ALL DEPENDS
    ${SHADER_BIN_DIR}/physics.spv
    ${SHADER_BIN_DIR}/fullscreen.vert.spv
    ${SHADER_BIN_DIR}/fullscreen.frag.spv
    ${SHADER_BIN_DIR}/overlay.vert.spv
    ${SHADER_BIN_DIR}/overlay.frag.spv
    ${SHADER_BIN_DIR}/cosmos_rt.vert.spv
    ${SHADER_BIN_DIR}/cosmos_rt.frag.spv
    ${SHADER_BIN_DIR}/cosmos_bh.spv
    ${SHADER_BIN_DIR}/cosmos_nebula.spv
    ${SHADER_BIN_DIR}/biochem_rt.vert.spv
    ${SHADER_BIN_DIR}/biochem_rt.frag.spv
)
add_dependencies(particle_physics shaders)
add_dependencies(particle_cosmos shaders)
add_dependencies(particle_biochem shaders)

# ── Portable build: embed shaders + icons into the binary ─────────────────────

option(PORTABLE_BUILD "Embed shaders and icons into the executable" OFF)
option(PORTABLE_PATHS "Use relative saves/ directory instead of platform-standard paths" OFF)
if(PORTABLE_PATHS)
    target_compile_definitions(particle_physics PRIVATE PORTABLE_PATHS)
endif()

if(PORTABLE_BUILD)
    set(EMBED_DIR ${CMAKE_BINARY_DIR}/generated)
    file(MAKE_DIRECTORY ${EMBED_DIR})

    # Helper: convert a binary file into a C++ source with a byte array
    macro(embed_resource INPUT SYMBOL)
        add_custom_command(
            OUTPUT  ${EMBED_DIR}/${SYMBOL}.cpp
            COMMAND ${CMAKE_COMMAND}
                    -DINPUT=${INPUT}
                    -DOUTPUT=${EMBED_DIR}/${SYMBOL}.cpp
                    -DSYMBOL=${SYMBOL}
                    -P ${CMAKE_SOURCE_DIR}/cmake/embed_resource.cmake
            DEPENDS ${INPUT}
            COMMENT "Embedding resource: ${SYMBOL}"
            VERBATIM
        )
    endmacro()

    # Shaders (depend on compiled SPIR-V)
    embed_resource(${SHADER_BIN_DIR}/physics.spv              physics_spv)
    embed_resource(${SHADER_BIN_DIR}/fullscreen.vert.spv      fullscreen_vert_spv)
    embed_resource(${SHADER_BIN_DIR}/fullscreen.frag.spv      fullscreen_frag_spv)
    embed_resource(${SHADER_BIN_DIR}/overlay.vert.spv         overlay_vert_spv)
    embed_resource(${SHADER_BIN_DIR}/overlay.frag.spv         overlay_frag_spv)
    embed_resource(${SHADER_BIN_DIR}/cosmos_rt.vert.spv       cosmos_rt_vert_spv)
    embed_resource(${SHADER_BIN_DIR}/cosmos_rt.frag.spv       cosmos_rt_frag_spv)
    embed_resource(${SHADER_BIN_DIR}/biochem_rt.vert.spv      biochem_rt_vert_spv)
    embed_resource(${SHADER_BIN_DIR}/biochem_rt.frag.spv      biochem_rt_frag_spv)

    # Icons
    embed_resource(${CMAKE_SOURCE_DIR}/assets/icon_32.png     icon_32_png)
    embed_resource(${CMAKE_SOURCE_DIR}/assets/icon_64.png     icon_64_png)
    embed_resource(${CMAKE_SOURCE_DIR}/assets/icon_256.png    icon_256_png)

    set(EMBEDDED_SOURCES
        ${EMBED_DIR}/physics_spv.cpp
        ${EMBED_DIR}/fullscreen_vert_spv.cpp
        ${EMBED_DIR}/fullscreen_frag_spv.cpp
        ${EMBED_DIR}/overlay_vert_spv.cpp
        ${EMBED_DIR}/overlay_frag_spv.cpp
        ${EMBED_DIR}/cosmos_rt_vert_spv.cpp
        ${EMBED_DIR}/cosmos_rt_frag_spv.cpp
        ${EMBED_DIR}/biochem_rt_vert_spv.cpp
        ${EMBED_DIR}/biochem_rt_frag_spv.cpp
        ${EMBED_DIR}/icon_32_png.cpp
        ${EMBED_DIR}/icon_64_png.cpp
        ${EMBED_DIR}/icon_256_png.cpp
    )

    target_sources(particle_physics PRIVATE ${EMBEDDED_SOURCES})
    target_include_directories(particle_physics PRIVATE ${EMBED_DIR})
    target_compile_definitions(particle_physics PRIVATE PORTABLE_BUILD)
endif()

# ── Install ───────────────────────────────────────────────────────────────────

install(TARGETS particle_physics pp_launcher particle_cosmos particle_biochem
        DESTINATION bin)
install(DIRECTORY ${SHADER_BIN_DIR}/ DESTINATION bin/shaders)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/ DESTINATION bin/assets)
