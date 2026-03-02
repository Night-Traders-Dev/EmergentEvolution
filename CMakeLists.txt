cmake_minimum_required(VERSION 3.20)
project(particle_playground VERSION 0.1.0 LANGUAGES C CXX)

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
find_package(CURL QUIET)

# ImGui via FetchContent (no install needed)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.0
)
FetchContent_MakeAvailable(imgui)

# ── Physics Simulation ───────────────────────────────────────────────────────

add_executable(particle_physics
    src/physics/main.cpp
    src/physics/simulation.cpp
    src/physics/phys_particles.cpp
    src/physics/interface.cpp
    src/physics/save_load.cpp
    src/physics/achievements.cpp
    src/physics/audio.cpp
    src/physics/error_dialog.cpp
    src/physics/steam_integration.cpp

    # Physics process modules (split from simulation.cpp)
    src/physics/nuclear.cpp
    src/physics/orbital.cpp
    src/physics/decay.cpp
    src/physics/meson_decays.cpp
    src/physics/quantum.cpp
    src/physics/spawning.cpp

    # Onboarding & gameplay
    src/physics/tutorial.cpp
    src/physics/scenarios.cpp

    # UI component modules (split from interface.cpp)
    src/physics/ui_panels.cpp
    src/physics/ui_cards.cpp
    src/physics/ui_lists.cpp
    src/physics/ui_dialogs.cpp
    src/physics/ui_tools.cpp
    src/physics/ui_overlays.cpp
    src/physics/ui_repository.cpp

    # Online repository
    src/physics/repository.cpp
    src/stb_image_impl.cpp
    src/stb_image_write_impl.cpp

    # Shared infrastructure
    src/vulkan_context.cpp
    src/compute_pipeline.cpp
    src/renderer.cpp
    src/particles.cpp
    src/particle_textures.cpp

    # ImGui core
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp

    # ImGui platform/renderer backends
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)

target_include_directories(particle_physics PRIVATE
    src/
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

# Threads (required by miniaudio audio engine)
find_package(Threads REQUIRED)

target_link_libraries(particle_physics PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
    ${CMAKE_DL_LIBS}     # dlopen for miniaudio backend loading
    Threads::Threads      # pthreads for miniaudio
)
# libm — needed on Linux/MinGW, not MSVC (math is built-in)
if(NOT MSVC)
    target_link_libraries(particle_physics PRIVATE m)
endif()
# Windows: link system libs for audio (WASAPI), windowing, etc.
if(WIN32)
    target_link_libraries(particle_physics PRIVATE ole32 winmm shell32)
    # .exe icon + version info resource
    # windres resolves paths relative to CWD (the build dir), so add source root as include path
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -I ${CMAKE_SOURCE_DIR}")
    target_sources(particle_physics PRIVATE assets/particle_playground.rc)
endif()

# OpenMP — link if available, define HAS_OPENMP so source can guard omp calls
if(OpenMP_FOUND)
    target_link_libraries(particle_physics PRIVATE OpenMP::OpenMP_CXX)
    target_compile_definitions(particle_physics PRIVATE HAS_OPENMP)
endif()

# Steamworks — link if available, define HAS_STEAM so source can guard Steam API calls
if(Steamworks_FOUND)
    target_link_libraries(particle_physics PRIVATE Steamworks::Steamworks)
    target_compile_definitions(particle_physics PRIVATE HAS_STEAM)
endif()

# libcurl + cJSON — link if available, define HAS_CURL for repository features
if(CURL_FOUND)
    target_sources(particle_physics PRIVATE src/cjson/cJSON.c)
    target_link_libraries(particle_physics PRIVATE CURL::libcurl)
    target_compile_definitions(particle_physics PRIVATE HAS_CURL)
endif()

# Platform-specific compiler flags
target_compile_options(particle_physics PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU,Clang>>:-O3>
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:MSVC>>:/O2>
)

# Version info — available in source as APP_VERSION string and component ints
target_compile_definitions(particle_physics PRIVATE
    APP_VERSION="${CMAKE_PROJECT_VERSION}"
    APP_VERSION_MAJOR=${CMAKE_PROJECT_VERSION_MAJOR}
    APP_VERSION_MINOR=${CMAKE_PROJECT_VERSION_MINOR}
    APP_VERSION_PATCH=${CMAKE_PROJECT_VERSION_PATCH}
)

# Windows: prevent <windows.h> macro pollution (min/max, etc.)
target_compile_definitions(particle_physics PRIVATE
    $<$<PLATFORM_ID:Windows>:NOMINMAX WIN32_LEAN_AND_MEAN>
)

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

add_custom_target(shaders ALL DEPENDS
    ${SHADER_BIN_DIR}/physics.spv
    ${SHADER_BIN_DIR}/fullscreen.vert.spv
    ${SHADER_BIN_DIR}/fullscreen.frag.spv
    ${SHADER_BIN_DIR}/overlay.vert.spv
    ${SHADER_BIN_DIR}/overlay.frag.spv
)
add_dependencies(particle_physics shaders)

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
        ${EMBED_DIR}/icon_32_png.cpp
        ${EMBED_DIR}/icon_64_png.cpp
        ${EMBED_DIR}/icon_256_png.cpp
    )

    target_sources(particle_physics PRIVATE ${EMBEDDED_SOURCES})
    target_include_directories(particle_physics PRIVATE ${EMBED_DIR})
    target_compile_definitions(particle_physics PRIVATE PORTABLE_BUILD)
endif()

# ── Install ───────────────────────────────────────────────────────────────────

install(TARGETS particle_physics DESTINATION bin)
install(DIRECTORY ${SHADER_BIN_DIR}/ DESTINATION bin/shaders)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/ DESTINATION bin/assets)
