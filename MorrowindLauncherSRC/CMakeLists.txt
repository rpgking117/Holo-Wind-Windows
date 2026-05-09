cmake_minimum_required(VERSION 3.15)
project(MorrowindLauncher VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# Self-contained build — no external F4SE SDK dependency required.
# Uses f4se_stubs.h for type definitions and PluginAPI ABI compatibility.
# Cross-compiles from Linux using mingw-w64.
# =============================================================================

add_library(MorrowindLauncher SHARED
    src/main.cpp
    src/MorrowindBridge.h
    src/f4se_stubs.h
)

target_include_directories(MorrowindLauncher PRIVATE
    src/
)

# Link against Windows system libraries provided by mingw-w64
target_link_libraries(MorrowindLauncher PRIVATE
    uuid
    d3d11
    dxgi
)

# Suppress mingw's default lib prefix (libXXX.dll -> XXX.dll)
set_target_properties(MorrowindLauncher PROPERTIES
    PREFIX ""
    OUTPUT_NAME "MorrowindLauncher"
    SUFFIX ".dll"
)

# Static link libgcc/libstdc++ so no mingw runtime DLLs are needed in Wine
target_link_options(MorrowindLauncher PRIVATE -static-libgcc -static-libstdc++ -static)

# Strip debug symbols for smaller DLL
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_link_options(MorrowindLauncher PRIVATE -s)
endif()

# Post-build: copy to mod output folder
add_custom_command(TARGET MorrowindLauncher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:MorrowindLauncher>"
        "${CMAKE_SOURCE_DIR}/../Mod/Data/F4SE/Plugins/MorrowindLauncher.dll"
    COMMENT "Deploying MorrowindLauncher.dll to Mod folder"
)
