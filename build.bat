@echo off
REM Build script for Windows
REM Requires: Vulkan SDK installed (https://vulkan.lunarg.com/)
REM GLFW and GLM are fetched automatically by CMake if not found.

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
    echo CMake configure failed.
    exit /b 1
)

cmake --build build --config Release
if errorlevel 1 (
    echo Build failed.
    exit /b 1
)

echo.
echo Build succeeded. Run: build\Release\particle_physics.exe
