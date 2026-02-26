cmake_minimum_required(VERSION 3.20)
project(particle_playground LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Dependencies ──────────────────────────────────────────────────────────────

find_package(Vulkan REQUIRED)
find_package(glfw3  REQUIRED)
find_package(glm    REQUIRED)
find_package(OpenMP REQUIRED)

# ImGui via FetchContent (no install needed)
include(FetchContent)
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
    OpenMP::OpenMP_CXX
)

target_compile_options(particle_physics PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Release>:-O3>
)

# ── Shader compilation ────────────────────────────────────────────────────────

# glslc is shipped with the Vulkan SDK; CMake 3.20+ exposes it
if(NOT Vulkan_GLSLC_EXECUTABLE)
    find_program(Vulkan_GLSLC_EXECUTABLE glslc
        HINTS "$ENV{VULKAN_SDK}/bin" /usr/bin /usr/local/bin)
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
