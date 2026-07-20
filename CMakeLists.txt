cmake_minimum_required(VERSION 3.24)
project(CleriaCore LANGUAGES C CXX)  # C is only for the vendored stb_vorbis decoder

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
  src/cleria/enemies.cpp
  src/cleria/cast.cpp
  src/cleria/gameinfo.cpp
  src/cleria/eex.cpp
  src/cleria/aia.cpp
  src/cleria/sfo.cpp
  src/cleria/scm.cpp
  src/cleria/sef.cpp
  src/cleria/sct.cpp
  src/cleria/aso.cpp
  src/cleria/csprite.cpp
  src/cleria/minifont.cpp
  src/cleria/effectdef.cpp
  src/cleria/scenelist.cpp
  src/cleria/bgmdef.cpp
  src/cleria/surface.cpp
  src/cleria/world.cpp)
target_include_directories(cleria PUBLIC src)

# cleria-dump — load a room and export it to .obj (no GPU).
add_executable(cleria-dump apps/cleria-dump/main.cpp)
target_link_libraries(cleria-dump PRIVATE cleria)

add_executable(cleria-xso apps/cleria-xso/main.cpp)
target_link_libraries(cleria-xso PRIVATE cleria)

# cleria-aiatest — decode an AIA sprite region to a PPM (verify vs aia_decode.py).
add_executable(cleria-aiatest apps/cleria-aiatest/main.cpp)
target_link_libraries(cleria-aiatest PRIVATE cleria)

# cleria-enemytest — assert ENEMYINFO's drop-table invariants against the corpus.
add_executable(cleria-enemytest apps/cleria-enemytest/main.cpp)
target_link_libraries(cleria-enemytest PRIVATE cleria)

# cleria-worldtest — spawn real actors from the real tables and hit one.
add_executable(cleria-worldtest apps/cleria-worldtest/main.cpp)
target_link_libraries(cleria-worldtest PRIVATE cleria)

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

# Ogg Vorbis decoder for the music streamer — nothings/stb v1.22, public domain,
# vendored verbatim. Built on its own so its warnings stay out of our /W4 build.
add_library(stb_vorbis STATIC "${TP}/stb/stb_vorbis.c")
target_include_directories(stb_vorbis PUBLIC "${TP}/stb")
if(MSVC)
  target_compile_options(stb_vorbis PRIVATE /W0)
else()
  target_compile_options(stb_vorbis PRIVATE -w)
endif()

# Audio is split out because it is the one part of the core that needs SDL3;
# keeping it separate lets cleria-dump / cleria-xso stay dependency-free.
add_library(cleria-audio STATIC src/cleria/audio.cpp)
target_link_libraries(cleria-audio PUBLIC cleria sdl3 PRIVATE stb_vorbis)

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
