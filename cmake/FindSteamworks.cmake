# FindSteamworks.cmake — locate Valve's Steamworks SDK for optional Steam integration
#
# Inputs:
#   STEAMWORKS_SDK_DIR  (env or cmake var) — root of the SDK download
#
# Outputs:
#   Steamworks_FOUND              — TRUE if SDK located
#   Steamworks::Steamworks        — imported shared-library target
#
# Usage in CMakeLists.txt:
#   list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
#   find_package(Steamworks QUIET)
#   if(Steamworks_FOUND)
#       target_link_libraries(my_app PRIVATE Steamworks::Steamworks)
#       target_compile_definitions(my_app PRIVATE HAS_STEAM)
#   endif()

if(NOT STEAMWORKS_SDK_DIR)
    if(DEFINED ENV{STEAMWORKS_SDK_DIR})
        set(STEAMWORKS_SDK_DIR "$ENV{STEAMWORKS_SDK_DIR}")
    endif()
endif()

# Header check
find_path(STEAMWORKS_INCLUDE_DIR
    NAMES steam/steam_api.h
    PATHS "${STEAMWORKS_SDK_DIR}/public"
    NO_DEFAULT_PATH
)

# Platform-specific library name and path
if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_STEAM_LIB_SUFFIX "win64")
        set(_STEAM_LIB_NAME "steam_api64")
    else()
        set(_STEAM_LIB_SUFFIX "")
        set(_STEAM_LIB_NAME "steam_api")
    endif()
elseif(APPLE)
    set(_STEAM_LIB_SUFFIX "osx")
    set(_STEAM_LIB_NAME "steam_api")
else()
    set(_STEAM_LIB_SUFFIX "linux64")
    set(_STEAM_LIB_NAME "steam_api")
endif()

find_library(STEAMWORKS_LIBRARY
    NAMES ${_STEAM_LIB_NAME}
    PATHS "${STEAMWORKS_SDK_DIR}/redistributable_bin/${_STEAM_LIB_SUFFIX}"
    NO_DEFAULT_PATH
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Steamworks
    REQUIRED_VARS STEAMWORKS_LIBRARY STEAMWORKS_INCLUDE_DIR
)

if(Steamworks_FOUND AND NOT TARGET Steamworks::Steamworks)
    add_library(Steamworks::Steamworks SHARED IMPORTED)
    set_target_properties(Steamworks::Steamworks PROPERTIES
        IMPORTED_LOCATION "${STEAMWORKS_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${STEAMWORKS_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(STEAMWORKS_INCLUDE_DIR STEAMWORKS_LIBRARY)
