cmake_minimum_required(VERSION 3.24)
project(CleriaCore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8 /EHsc)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra)
endif()

set(TP "${CMAKE_SOURCE_DIR}/third_party")

# Asset core — the reverse-engineered format parsers + room assembly.
add_library(cleria STATIC
  src/cleria/ymo.cpp
  src/cleria/sob.cpp
  src/cleria/scene.cpp
  src/cleria/dds.cpp
  src/cleria/sen.cpp
  src/cleria/physics.cpp
  src/cleria/xso.cpp
  src/cleria/items.cpp
  src/cleria/aia.cpp
  src/cleria/sfo.cpp
  src/cleria/scm.cpp
  src/cleria/sef.cpp
  src/cleria/sct.cpp
  src/cleria/aso.cpp
  src/cleria/effectdef.cpp
  src/cleria/surface.cpp)
target_include_directories(cleria PUBLIC src)

# cleria-dump — load a room and export it to .obj (no GPU).
add_executable(cleria-dump apps/cleria-dump/main.cpp)
target_link_libraries(cleria-dump PRIVATE cleria)

add_executable(cleria-xso apps/cleria-xso/main.cpp)
target_link_libraries(cleria-xso PRIVATE cleria)

# cleria-aiatest — decode an AIA sprite region to a PPM (verify vs aia_decode.py).
add_executable(cleria-aiatest apps/cleria-aiatest/main.cpp)
target_link_libraries(cleria-aiatest PRIVATE cleria)

# cleria-asotest — validate the ASO container + run the animation-script VM.
add_executable(cleria-asotest apps/cleria-asotest/main.cpp)
target_link_libraries(cleria-asotest PRIVATE cleria)

# cleria-animtest — reproduce the stored clip AABBs from our own animation+skinning.
add_executable(cleria-animtest apps/cleria-animtest/main.cpp)
target_link_libraries(cleria-animtest PRIVATE cleria)

# cleria-audiotest — dump the EFFECTDEF table, to diff against effectdef_parse.py.
add_executable(cleria-audiotest apps/cleria-audiotest/main.cpp)

# ---- third-party prebuilt: wgpu-native + SDL3 -------------------------------
add_library(wgpu_native SHARED IMPORTED)
set_target_properties(wgpu_native PROPERTIES
  IMPORTED_LOCATION "${TP}/wgpu/lib/wgpu_native.dll"
  IMPORTED_IMPLIB   "${TP}/wgpu/lib/wgpu_native.dll.lib"
  INTERFACE_INCLUDE_DIRECTORIES "${TP}/wgpu/include")

add_library(sdl3 SHARED IMPORTED)
set_target_properties(sdl3 PROPERTIES
  IMPORTED_LOCATION "${TP}/sdl3/lib/x64/SDL3.dll"
  IMPORTED_IMPLIB   "${TP}/sdl3/lib/x64/SDL3.lib"
  INTERFACE_INCLUDE_DIRECTORIES "${TP}/sdl3/include")

# Audio is split out because it is the one part of the core that needs SDL3;
# keeping it separate lets cleria-dump / cleria-xso stay dependency-free.
add_library(cleria-audio STATIC src/cleria/audio.cpp)
target_link_libraries(cleria-audio PUBLIC cleria sdl3)

target_link_libraries(cleria-audiotest PRIVATE cleria cleria-audio)

# cleria-view — real-time WebGPU renderer.
add_executable(cleria-view apps/cleria-view/main.cpp)
target_link_libraries(cleria-view PRIVATE cleria cleria-audio wgpu_native sdl3)

# Copy the runtime DLLs next to the viewer so it runs from the build dir.
add_custom_command(TARGET cleria-view POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${TP}/wgpu/lib/wgpu_native.dll" "$<TARGET_FILE_DIR:cleria-view>"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${TP}/sdl3/lib/x64/SDL3.dll" "$<TARGET_FILE_DIR:cleria-view>"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/res/hud_font.bin" "$<TARGET_FILE_DIR:cleria-view>")
