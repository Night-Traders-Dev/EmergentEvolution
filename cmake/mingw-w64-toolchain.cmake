# MinGW-w64 cross-compilation toolchain for building Windows binaries on Linux
# Usage: cmake -S . -B build-win64 -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-toolchain.cmake

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Compilers
set(CMAKE_C_COMPILER   x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER  x86_64-w64-mingw32-windres)

# Search paths — search sysroot + any extra roots passed via CMAKE_FIND_ROOT_PATH
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)   # use host tools (cmake, glslc)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)    # search sysroot + custom roots
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)    # search sysroot + custom roots
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

# Static linking of libgcc/libstdc++ so the .exe runs without shipping MinGW DLLs
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static-libgcc -static-libstdc++")
