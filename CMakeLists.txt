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
file(GLOB CLERIA_BRAINS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/cleria/brains/*.cpp)
add_library(cleria STATIC ${CLERIA_BRAINS}
  src/cleria/ymo.cpp
  src/cleria/sob.cpp
  src/cleria/scene.cpp
  src/cleria/dds.cpp
  src/cleria/sen.cpp
  src/cleria/physics.cpp
  src/cleria/enemy_ai.cpp
  src/cleria/xso.cpp
  src/cleria/sev.cpp
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
  src/cleria/ef2.cpp
  src/cleria/ef2_sim.cpp
  src/cleria/bullet.cpp
  src/cleria/meshobj.cpp
  src/cleria/inventory.cpp
  src/cleria/menusprite.cpp
  src/cleria/pausebook.cpp
  src/cleria/wndtext.cpp
  src/cleria/pickup.cpp
  src/cleria/richtext.cpp
  src/cleria/wnddialog.cpp
  src/cleria/titlescreen.cpp
  src/cleria/popup.cpp
  src/cleria/eventsprites.cpp
  src/cleria/boss01.cpp
  src/cleria/boss02.cpp
  src/cleria/boss03.cpp
  src/cleria/player_actions.cpp
  src/cleria/locus.cpp
  src/cleria/emotion.cpp
  src/cleria/camrig.cpp
  src/cleria/skillmp.cpp
  src/cleria/surface.cpp
  src/cleria/save.cpp
  src/cleria/world.cpp
  src/cleria/input_map.cpp
  src/cleria/settings.cpp
  src/cleria/ni_archive.cpp
  src/cleria/gamedata.cpp)
target_include_directories(cleria PUBLIC src)

# miniz 3.0.2 (richgel999/miniz, MIT), vendored verbatim in third_party/miniz: inflate + crc32
# for the game's NI/NA archives (src/cleria/ni_archive). Own target so its warnings stay out of /W4.
add_library(miniz STATIC "${TP}/miniz/miniz.c")
target_include_directories(miniz PUBLIC "${TP}/miniz")
if(MSVC)
  target_compile_options(miniz PRIVATE /W0)
else()
  target_compile_options(miniz PRIVATE -w)
endif()
target_link_libraries(cleria PRIVATE miniz)

# cleria-gamedata — find / validate a Ys Origin install and unpack it (src/cleria/gamedata).
add_executable(cleria-gamedata apps/cleria-gamedata/main.cpp)
target_link_libraries(cleria-gamedata PRIVATE cleria)

# cleria-gamedatatest — the NI/NA reader vs the real install and assets/ (skips without them).
add_executable(cleria-gamedatatest apps/cleria-gamedatatest/main.cpp)
target_link_libraries(cleria-gamedatatest PRIVATE cleria)
if(WIN32)
  # settings.cpp: SHGetKnownFolderPath(FOLDERID_SavedGames) / CoTaskMemFree / GetUserNameA.
  target_link_libraries(cleria PUBLIC shell32 ole32 advapi32)
endif()

# cleria-dump — load a room and export it to .obj (no GPU).
add_executable(cleria-cotest apps/cleria-cotest/main.cpp)
target_link_libraries(cleria-cotest PRIVATE cleria)

# cleria-combattest — hit resolution (src/cleria/hit.hpp) against the disassembly's rules.
add_executable(cleria-combattest apps/cleria-combattest/main.cpp)
target_link_libraries(cleria-combattest PRIVATE cleria)

# cleria-movetest — player locomotion + vertical model (src/cleria/physics_player.hpp).
add_executable(cleria-movetest apps/cleria-movetest/main.cpp)
target_link_libraries(cleria-movetest PRIVATE cleria)

# cleria-ef2simtest — the EF2 particle simulation vs build/ai_spec/EF2SIM.md §5.
add_executable(cleria-ef2simtest apps/cleria-ef2simtest/main.cpp)
target_link_libraries(cleria-ef2simtest PRIVATE cleria)

# cleria-bullettest — the GoXx bullet vs build/ai_spec/BULLETS.md.
add_executable(cleria-bullettest apps/cleria-bullettest/main.cpp)
target_link_libraries(cleria-bullettest PRIVATE cleria)

# cleria-meshtest — the scripted map-object runtime vs build/event_re/WAITOPS.md.
add_executable(cleria-meshtest apps/cleria-meshtest/main.cpp)
target_link_libraries(cleria-meshtest PRIVATE cleria)

# cleria-invtest — inventory / equipment / stats vs build/progress_re/INVENTORY.md.
add_executable(cleria-invtest apps/cleria-invtest/main.cpp)
target_link_libraries(cleria-invtest PRIVATE cleria)

# cleria-booktest — the pause book vs build/menu_re/PAUSEBOOK_DRAW.md.
add_executable(cleria-booktest apps/cleria-booktest/main.cpp)
target_link_libraries(cleria-booktest PRIVATE cleria)

# cleria-camtest — the region camera rig (src/cleria/camrig) vs build/event_re/CAMERA_OPS.md.
add_executable(cleria-camtest apps/cleria-camtest/main.cpp)
target_link_libraries(cleria-camtest PRIVATE cleria)

# cleria-skilltest — MP, the skill charge/release and the Boost rules (src/cleria/skillmp).
add_executable(cleria-skilltest apps/cleria-skilltest/main.cpp)
target_link_libraries(cleria-skilltest PRIVATE cleria)

# cleria-bosstest — Velagunder's brain (src/cleria/boss01) vs build/boss_re/VELAGUNDER.md.
add_executable(cleria-bosstest apps/cleria-bosstest/main.cpp)
target_link_libraries(cleria-bosstest PRIVATE cleria)

# cleria-boss02test — Nygtilger's brain (src/cleria/boss02) vs build/boss_re/NYGTILGER.md.
add_executable(cleria-boss02test apps/cleria-boss02test/main.cpp)
target_link_libraries(cleria-boss02test PRIVATE cleria)

# cleria-boss03test — Gelaldy's brain (src/cleria/boss03) vs build/boss_re/GELALDY.md.
add_executable(cleria-boss03test apps/cleria-boss03test/main.cpp)
target_link_libraries(cleria-boss03test PRIVATE cleria)

# cleria-chrtest — the cutscene actor ops (build/event_re/CHR_OPS.md): facing, pattern, tremble, fly.
add_executable(cleria-chrtest apps/cleria-chrtest/main.cpp)
target_link_libraries(cleria-chrtest PRIVATE cleria)

# cleria-actiontest — the player's action input (src/cleria/player_actions).
add_executable(cleria-actiontest apps/cleria-actiontest/main.cpp)
target_link_libraries(cleria-actiontest PRIVATE cleria)

# cleria-locustest — the GoLocusSword hit arm (src/cleria/locus) vs build/ability_re/LOCUS.md.
add_executable(cleria-locustest apps/cleria-locustest/main.cpp)
target_link_libraries(cleria-locustest PRIVATE cleria)

# cleria-picktest — world item pickups vs build/item_re/PICKUPS.md.
add_executable(cleria-picktest apps/cleria-picktest/main.cpp)
target_link_libraries(cleria-picktest PRIVATE cleria)

# cleria-wndtest — the event windows and their text vs build/event_re/WNDDIALOG_DRAW.md.
add_executable(cleria-wndtest apps/cleria-wndtest/main.cpp)
target_link_libraries(cleria-wndtest PRIVATE cleria)

# cleria-settingstest — the yso_win.ini dialect / cleria.ini / import vs build/shell_re/CONFIG.md §1-2.
add_executable(cleria-settingstest apps/cleria-settingstest/main.cpp)
target_link_libraries(cleria-settingstest PRIVATE cleria)

# cleria-inputtest — the binding tables and the mapper FUN_005d2ba0 vs build/shell_re/CONFIG.md §3.
add_executable(cleria-inputtest apps/cleria-inputtest/main.cpp)
target_link_libraries(cleria-inputtest PRIVATE cleria)

# cleria-aitest — enemy AI machinery + the M100 brain (src/cleria/enemy_ai.hpp).
add_executable(cleria-aitest apps/cleria-aitest/main.cpp)
target_link_libraries(cleria-aitest PRIVATE cleria)

# cleria-savetest — save-codec round-trip (SAVELOAD.md; real-save read via CLERIA_REAL_SAVE).
add_executable(cleria-savetest apps/cleria-savetest/main.cpp)
target_link_libraries(cleria-savetest PRIVATE cleria)

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

# cleria-collisiontest — self-verifying tests for collide_and_slide (headless).
add_executable(cleria-collisiontest apps/cleria-collisiontest/main.cpp)
target_link_libraries(cleria-collisiontest PRIVATE cleria)

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

# Dear ImGui (ocornut/imgui v1.92.9b, MIT), vendored verbatim in third_party/imgui: the shell
# overlay (apps/cleria-view/view_shell.inl). Built on its own so its warnings stay out of /W4.
add_library(imgui STATIC
  "${TP}/imgui/imgui.cpp" "${TP}/imgui/imgui_draw.cpp" "${TP}/imgui/imgui_tables.cpp"
  "${TP}/imgui/imgui_widgets.cpp"
  "${TP}/imgui/backends/imgui_impl_sdl3.cpp" "${TP}/imgui/backends/imgui_impl_wgpu.cpp")
target_include_directories(imgui PUBLIC "${TP}/imgui")
# wgpu-native v29 follows the standardized webgpu.h (WGPUComputeState, chained vertex
# attributes) that the backend's DAWN path targets; its WGPU path is for older wgpu-native.
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_WEBGPU_BACKEND_DAWN)
target_link_libraries(imgui PUBLIC wgpu_native sdl3)
if(MSVC)
  target_compile_options(imgui PRIVATE /W0)
else()
  target_compile_options(imgui PRIVATE -w)
endif()

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

# The demo movies (src/cleria/movie): the engine's tables + a Media Foundation video decoder
# (Windows built-ins only; a stub elsewhere), split out like the audio for its system libs.
add_library(cleria-movie STATIC src/cleria/movie.cpp)
target_link_libraries(cleria-movie PUBLIC cleria)
if(WIN32)
  target_link_libraries(cleria-movie PRIVATE mfplat mfreadwrite mfuuid ole32)
endif()
add_executable(cleria-movietest apps/cleria-movietest/main.cpp)
target_link_libraries(cleria-movietest PRIVATE cleria cleria-movie)

# cleria-view — real-time WebGPU renderer.
add_executable(cleria-view apps/cleria-view/main.cpp)
target_link_libraries(cleria-view PRIVATE cleria cleria-audio cleria-movie imgui wgpu_native sdl3)

# Copy the runtime DLLs next to the viewer so it runs from the build dir.
add_custom_command(TARGET cleria-view POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${TP}/wgpu/lib/wgpu_native.dll" "$<TARGET_FILE_DIR:cleria-view>"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${TP}/sdl3/lib/x64/SDL3.dll" "$<TARGET_FILE_DIR:cleria-view>"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/res/hud_font.bin" "$<TARGET_FILE_DIR:cleria-view>"
  # the shell's fonts (Fira Sans / Fira Sans Condensed, SIL OFL; third_party/fonts)
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${TP}/fonts" "$<TARGET_FILE_DIR:cleria-view>/fonts")

# ---- tests ------------------------------------------------------------------
# The self-verifying test apps, registered so `ctest -C Release` runs the suite.
# All take the extracted asset root and exit non-zero on any failed assert. The root is
# CLERIA_TEST_ASSETS: by default CleriaCore's data folder when an unpack has completed there
# ($CLERIA_DATA, else %LOCALAPPDATA%/CleriaCore/data; see src/cleria/gamedata.hpp), else the
# legacy `assets/` in the source dir.
if(DEFINED ENV{CLERIA_DATA})
  set(_cleria_data "$ENV{CLERIA_DATA}")
else()
  set(_cleria_data "$ENV{LOCALAPPDATA}/CleriaCore/data")
endif()
file(TO_CMAKE_PATH "${_cleria_data}" _cleria_data)
if(EXISTS "${_cleria_data}/cleria-gamedata.manifest" AND NOT EXISTS "${_cleria_data}/.cleria-installing")
  set(_cleria_assets_default "${_cleria_data}")
else()
  set(_cleria_assets_default "${CMAKE_SOURCE_DIR}/assets")
endif()
set(CLERIA_TEST_ASSETS "${_cleria_assets_default}" CACHE PATH "Asset root the ctest suite runs on")
enable_testing()
foreach(t collisiontest cotest worldtest enemytest asotest animtest savetest combattest movetest aitest ef2simtest bullettest meshtest invtest booktest picktest wndtest bosstest boss02test boss03test camtest skilltest actiontest locustest chrtest movietest settingstest inputtest gamedatatest)
  add_test(NAME ${t}
           COMMAND cleria-${t} "${CLERIA_TEST_ASSETS}"
           WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endforeach()
