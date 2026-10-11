# Retail probe for Ys Origin (yso_win.exe v1.1.1.0, Steam) - a 32-bit dinput8.dll
# proxy with a Dear ImGui panel, built the same way as the Archipelago mod.
#
#   cmake -A Win32 -B build && cmake --build build --config Release
# (or run build.ps1, which also reuses already-downloaded MinHook / ImGui sources
# and copies the result into dist/).
cmake_minimum_required(VERSION 3.20)
project(yso_retail_probe C CXX)   # C: MinHook's sources

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Configure a 32-bit build (cmake -A Win32). The game is 32-bit.")
endif()

include(FetchContent)
FetchContent_Declare(minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        v1.3.3)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5)
foreach(dep minhook imgui)
    FetchContent_GetProperties(${dep})
    if(NOT ${dep}_POPULATED)
        FetchContent_Populate(${dep})
    endif()
endforeach()

add_library(dinput8 SHARED
    src/hooks.cpp
    src/probe.cpp
    src/panel.cpp
    src/tests.cpp
    src/simapi.cpp
    src/exports.def
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx9.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${minhook_SOURCE_DIR}/src/buffer.c
    ${minhook_SOURCE_DIR}/src/hook.c
    ${minhook_SOURCE_DIR}/src/trampoline.c
    ${minhook_SOURCE_DIR}/src/hde/hde32.c
    ${minhook_SOURCE_DIR}/src/hde/hde64.c)
target_include_directories(dinput8 PRIVATE
    src ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${minhook_SOURCE_DIR}/include)
target_link_libraries(dinput8 PRIVATE d3d9)
target_compile_definitions(dinput8 PRIVATE WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS NOMINMAX)
# Static CRT: the DLL then needs nothing but system DLLs next to the game.
set_property(TARGET dinput8 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set_target_properties(dinput8 PROPERTIES PREFIX "" OUTPUT_NAME "dinput8")

# Offline check of retail_probe_tests.ini with the same parser (two small files).
add_executable(probe_selftest src/selftest.cpp src/tests.cpp)
target_compile_definitions(probe_selftest PRIVATE _CRT_SECURE_NO_WARNINGS)
set_property(TARGET probe_selftest PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Offline simulation of the game-side logic against a fake memory image mapped at
# the exe's addresses, moved to free memory (src/simtest.cpp).
add_executable(probe_simtest src/simtest.cpp)
target_compile_definitions(probe_simtest PRIVATE _CRT_SECURE_NO_WARNINGS)
set_property(TARGET probe_simtest PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Overlay check: a bare Direct3D 9 window that loads the DLL and saves a screenshot
# of the panel (src/uitest.cpp). Not run by build.ps1.
add_executable(probe_uitest src/uitest.cpp)
target_link_libraries(probe_uitest PRIVATE d3d9)
target_compile_definitions(probe_uitest PRIVATE _CRT_SECURE_NO_WARNINGS)
set_property(TARGET probe_uitest PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
