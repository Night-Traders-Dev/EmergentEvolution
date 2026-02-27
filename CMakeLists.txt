cmake_minimum_required(VERSION 3.20)
project(particle_playground LANGUAGES CXX)

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
find_package(glm QUIET)
if(NOT glm_FOUND)
    message(STATUS "glm not found via find_package — fetching with FetchContent")
    FetchContent_Declare(glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        1.0.1
    )
    FetchContent_MakeAvailable(glm)
endif()

# OpenMP — optional (MSVC only supports OpenMP 2.0; builds fine without it)
find_package(OpenMP QUIET)

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
    src/stb_image_impl.cpp

    # Shared infrastructure
    src/vulkan_context.cpp
    src/compute_pipeline.cpp
    src/renderer.cpp
    src/particles.cpp

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

target_link_libraries(particle_physics PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
)

# OpenMP — link if available, define HAS_OPENMP so source can guard omp calls
if(OpenMP_FOUND)
    target_link_libraries(particle_physics PRIVATE OpenMP::OpenMP_CXX)
    target_compile_definitions(particle_physics PRIVATE HAS_OPENMP)
endif()

# Platform-specific compiler flags
target_compile_options(particle_physics PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU,Clang>>:-O3>
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:MSVC>>:/O2>
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

add_custom_target(shaders ALL DEPENDS
    ${SHADER_BIN_DIR}/physics.spv
    ${SHADER_BIN_DIR}/fullscreen.vert.spv
    ${SHADER_BIN_DIR}/fullscreen.frag.spv
)
add_dependencies(particle_physics shaders)

# ── Install ───────────────────────────────────────────────────────────────────

install(TARGETS particle_physics DESTINATION bin)
install(DIRECTORY ${SHADER_BIN_DIR}/ DESTINATION bin/shaders)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/ DESTINATION bin/assets)
