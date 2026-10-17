cmake_minimum_required(VERSION 3.24)
project(CleriaCore LANGUAGES C CXX)  # C is only for the vendored stb_vorbis decoder

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT WIN32 AND NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)   # single-config generators: optimised
endif()

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8 /EHsc)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra)
  # No fused multiply-add: a * b + c rounds twice, as MSVC's /fp:precise does on x64. clang contracts by
  # default (skilltest's slow fall differed by one ulp in CI), and gcc does wherever the CPU has FMA (arm64).
  add_compile_options(-ffp-contract=off)
  # The C library's transcendentals are the Windows build's (src/cleria/platform_math.cpp): no call may be folded
  # at compile time (gcc folds with MPFR's correctly rounded result) or fused into sincos.
  foreach(fn sin sinf cos cosf tan tanf sincos sincosf atan2 atan2f asin asinf acos acosf exp log log10 pow powf hypotf)
    add_compile_options(-fno-builtin-${fn})
  endforeach()
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
  src/cleria/weather.cpp
  src/cleria/bullet.cpp
  src/cleria/meshobj.cpp
  src/cleria/inventory.cpp
  src/cleria/menusprite.cpp
  src/cleria/pausebook.cpp
  src/cleria/wndtext.cpp
  src/cleria/pickup.cpp
  src/cleria/richtext.cpp
  src/cleria/extfont.cpp
  src/cleria/wnddialog.cpp
  src/cleria/titlescreen.cpp
  src/cleria/bonusshop.cpp
  src/cleria/stageselect.cpp
  src/cleria/popup.cpp
  src/cleria/eventsprites.cpp
  src/cleria/boss01.cpp
  src/cleria/boss02.cpp
  src/cleria/boss03.cpp
  src/cleria/boss04.cpp
  src/cleria/boss05.cpp
  src/cleria/boss06.cpp
  src/cleria/boss07.cpp
  src/cleria/boss08.cpp
  src/cleria/player_actions.cpp
  src/cleria/locus.cpp
  src/cleria/emotion.cpp
  src/cleria/camrig.cpp
  src/cleria/skillmp.cpp
  src/cleria/surface.cpp
  src/cleria/save.cpp
  src/cleria/records.cpp
  src/cleria/world.cpp
  src/cleria/input_map.cpp
  src/cleria/mouse.cpp
  src/cleria/vfs.cpp
  src/cleria/mods.cpp
  src/cleria/settings.cpp
  src/cleria/access.cpp
  src/cleria/pad.cpp
  src/cleria/video.cpp
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
# cleria-xso13test — the floor 1..3 XSO op pass (build/event_re/FLOOR13_OPS.md).
add_executable(cleria-xso13test apps/cleria-xso13test/main.cpp)
target_link_libraries(cleria-xso13test PRIVATE cleria)
# cleria-dueltest — the story duels' script / engine side (build/event_re/DUELS.md).
add_executable(cleria-dueltest apps/cleria-dueltest/main.cpp)
target_link_libraries(cleria-dueltest PRIVATE cleria)
# cleria-weathertest — the floor-3 script weather vs the retail bodies (build/effect_re/WEATHER.md).
add_executable(cleria-weathertest apps/cleria-weathertest/main.cpp)
target_link_libraries(cleria-weathertest PRIVATE cleria)
# cleria-xsoops — the XSO VM op coverage probe for tools/xso_floor_ops.py (FLOOR13_OPS.md).
add_executable(cleria-xsoops apps/cleria-xsoops/main.cpp)
target_link_libraries(cleria-xsoops PRIVATE cleria)

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
# cleria-boss04test — Khonsclard's brain (src/cleria/boss04) vs build/boss_re/KHONSCLARD.md.
add_executable(cleria-boss04test apps/cleria-boss04test/main.cpp)
target_link_libraries(cleria-boss04test PRIVATE cleria)
# cleria-boss05test — Pictimos's brain (src/cleria/boss05) vs build/boss_re/PICTIMOS.md.
add_executable(cleria-boss05test apps/cleria-boss05test/main.cpp)
target_link_libraries(cleria-boss05test PRIVATE cleria)
# cleria-boss06test — Zava's two heads (src/cleria/boss06) vs build/boss_re/ZAVA*.md.
add_executable(cleria-boss06test apps/cleria-boss06test/main.cpp)
target_link_libraries(cleria-boss06test PRIVATE cleria)
# cleria-boss07test — Dalles (src/cleria/boss07) vs build/boss_re/DALLES*.md.
add_executable(cleria-boss07test apps/cleria-boss07test/main.cpp)
target_link_libraries(cleria-boss07test PRIVATE cleria)
# cleria-boss08test — Darm (src/cleria/boss08) vs build/boss_re/DARM*.md.
add_executable(cleria-boss08test apps/cleria-boss08test/main.cpp)
target_link_libraries(cleria-boss08test PRIVATE cleria)

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

# cleria-modstest — mod manifests, load order, dependency / conflict checks, zip mods and the vfs
# resolution order (src/cleria/mods, src/cleria/vfs; build/shell_re/MODS.md). Writes only under %TEMP%.
add_executable(cleria-modstest apps/cleria-modstest/main.cpp)
target_link_libraries(cleria-modstest PRIVATE cleria cleria-net miniz)

# Mod content (phase 2a, build/shell_re/CONTENT.md): the save sidecar, the event bus, content/*.ini.
target_sources(cleria PRIVATE src/cleria/inidoc.cpp src/cleria/sidecar.cpp src/cleria/mod_events.cpp
                              src/cleria/content.cpp)
# cleria-contenttest — content/*.ini parsing + merging, the grant filter on real chest scripts, the
# rules, text patches, the event bus and the sidecar (src/cleria/content, mod_events, sidecar).
add_executable(cleria-contenttest apps/cleria-contenttest/main.cpp)
target_link_libraries(cleria-contenttest PRIVATE cleria)

# cleria-settingstest — the yso_win.ini dialect / cleria.ini / import vs build/shell_re/CONFIG.md §1-2.
add_executable(cleria-settingstest apps/cleria-settingstest/main.cpp)
target_link_libraries(cleria-settingstest PRIVATE cleria)

# cleria-accesstest — the accessibility assists / cheats: the skip gate, the multipliers, the flash
# limiter, cleria.ini [cleria.access] / [cleria.cheats] (src/cleria/access, build/shell_re/ACCESS.md).
add_executable(cleria-accesstest apps/cleria-accesstest/main.cpp)
target_link_libraries(cleria-accesstest PRIVATE cleria)

# cleria-atan2ftest — the Windows build's atan2f on every platform (src/cleria/platform_math): on Windows against
# the C library's, bit for bit; everywhere the pinned results through std::atan2.
add_executable(cleria-atan2ftest apps/cleria-atan2ftest/main.cpp)
target_link_libraries(cleria-atan2ftest PRIVATE cleria)

# cleria-crtmathtest � the rest of the C library functions on every platform (src/cleria/msvc_*.cpp): the pinned
# Windows results everywhere, the linked functions being the ports, and on an FMA3 Windows runner the CRT itself.
add_executable(cleria-crtmathtest apps/cleria-crtmathtest/main.cpp)
target_link_libraries(cleria-crtmathtest PRIVATE cleria)

# cleria-inputtest — the binding tables and the mapper FUN_005d2ba0 vs build/shell_re/CONFIG.md §3.
add_executable(cleria-inputtest apps/cleria-inputtest/main.cpp)
target_link_libraries(cleria-inputtest PRIVATE cleria)

# cleria-padtest — the Controller page's logic: glyph sets, binding swaps, the analog-walk shaping, the
# swapped OK / Cancel and retail's force-feedback table (src/cleria/pad.hpp) vs build/shell_re/PAD.md.
add_executable(cleria-padtest apps/cleria-padtest/main.cpp)
target_link_libraries(cleria-padtest PRIVATE cleria)

# cleria-videotest - the Video page's pure part: retail's gamma ramp / sampler table / aspect / FPS text,
# cleria.ini [cleria.video], render scale, frame limiter, frame statistics (src/cleria/video, build/shell_re/VIDEO.md).
add_executable(cleria-videotest apps/cleria-videotest/main.cpp)
target_link_libraries(cleria-videotest PRIVATE cleria)

# cleria-mousetest — the cursor transform, the cursor manager, the sprite, click-to-move, the skill
# wheel and the menu hit tests (src/cleria/mouse.hpp) vs build/shell_re/MOUSE.md.
add_executable(cleria-mousetest apps/cleria-mousetest/main.cpp)
target_link_libraries(cleria-mousetest PRIVATE cleria)

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

# cleria-bodytest — the body push FUN_0045e460 (body_push.hpp), the player's two knock
# channels and the hit feedback tables (hit_feedback.hpp).
add_executable(cleria-bodytest apps/cleria-bodytest/main.cpp)
target_link_libraries(cleria-bodytest PRIVATE cleria)

# cleria-asotest — validate the ASO container + run the animation-script VM.
add_executable(cleria-asotest apps/cleria-asotest/main.cpp)
target_link_libraries(cleria-asotest PRIVATE cleria)

# cleria-animtest — reproduce the stored clip AABBs from our own animation+skinning.
add_executable(cleria-animtest apps/cleria-animtest/main.cpp)
target_link_libraries(cleria-animtest PRIVATE cleria)

# cleria-audiotest — dump the EFFECTDEF table, to diff against effectdef_parse.py.
add_executable(cleria-audiotest apps/cleria-audiotest/main.cpp)

# ---- third-party prebuilt: wgpu-native + SDL3 -------------------------------
if(WIN32)
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
else()
  # Linux / macOS (build/PORTING.md, issue #46). SDL3 through its CMake package: an install prefix in
  # SDL3_DIR / CMAKE_PREFIX_PATH, else third_party/sdl3 (tools/fetch_deps.py builds 3.4.12 into it).
  find_package(SDL3 3.4 CONFIG REQUIRED HINTS "${TP}/sdl3")
  add_library(sdl3 INTERFACE)
  target_link_libraries(sdl3 INTERFACE SDL3::SDL3)
  # wgpu-native: a release zip unpacked as include/ + lib/ (the Windows layout), default third_party/wgpu.
  set(WGPU_NATIVE_DIR "${TP}/wgpu" CACHE PATH "wgpu-native release folder (include/, lib/)")
  find_library(WGPU_NATIVE_LIB NAMES wgpu_native PATHS "${WGPU_NATIVE_DIR}/lib" NO_DEFAULT_PATH REQUIRED)
  if(APPLE)
    # The release dylib may carry an absolute install name: use a copy whose id is @rpath-relative, so
    # cleria-view finds the one copied next to it (the post-build step below).
    execute_process(COMMAND otool -D "${WGPU_NATIVE_LIB}" OUTPUT_VARIABLE _wgpu_id OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _wgpu_id MATCHES "@rpath/")
      set(_wgpu_copy "${CMAKE_BINARY_DIR}/wgpu-native/libwgpu_native.dylib")
      file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/wgpu-native")
      file(COPY_FILE "${WGPU_NATIVE_LIB}" "${_wgpu_copy}")
      execute_process(COMMAND install_name_tool -id "@rpath/libwgpu_native.dylib" "${_wgpu_copy}")
      execute_process(COMMAND codesign --force --sign - "${_wgpu_copy}")
      set(WGPU_NATIVE_LIB "${_wgpu_copy}")
    endif()
  endif()
  find_package(Threads REQUIRED)
  add_library(wgpu_native SHARED IMPORTED)
  set_target_properties(wgpu_native PROPERTIES
    IMPORTED_LOCATION "${WGPU_NATIVE_LIB}"
    IMPORTED_NO_SONAME TRUE
    INTERFACE_INCLUDE_DIRECTORIES "${WGPU_NATIVE_DIR}/include"
    INTERFACE_LINK_LIBRARIES "Threads::Threads;${CMAKE_DL_LIBS}")
  # The runtime libraries are copied next to cleria-view (below); find them there.
  if(APPLE)
    set(CMAKE_BUILD_RPATH "@loader_path")
  else()
    set(CMAKE_BUILD_RPATH "$ORIGIN")
  endif()
  target_link_libraries(cleria PUBLIC Threads::Threads)
  # iconv: the archives' CP932 names (src/cleria/ni_archive.cpp); in glibc, a library on macOS.
  find_package(Iconv REQUIRED)
  target_link_libraries(cleria PRIVATE Iconv::Iconv)
endif()

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
if(APPLE)
  # The backend's surface helper is Objective-C on macOS (Cocoa / CAMetalLayer).
  set_source_files_properties("${TP}/imgui/backends/imgui_impl_wgpu.cpp" PROPERTIES COMPILE_OPTIONS "-xobjective-c++")
  target_link_libraries(imgui PUBLIC "-framework Cocoa" "-framework QuartzCore" "-framework Metal")
endif()
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
else()
  # Off Windows the movies decode through FFmpeg (libavformat / libavcodec / libswscale) when this is
  # on; off, the decoder is a stub and every movie ends at once, as on retail's open failure.
  option(CLERIA_FFMPEG "Decode the demo movies with FFmpeg (Linux / macOS)" OFF)
  if(CLERIA_FFMPEG)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libswscale libavutil)
    target_compile_definitions(cleria-movie PRIVATE CLERIA_FFMPEG)
    target_link_libraries(cleria-movie PRIVATE PkgConfig::FFMPEG)
  endif()
endif()
add_executable(cleria-movietest apps/cleria-movietest/main.cpp)
target_link_libraries(cleria-movietest PRIVATE cleria cleria-movie)

# cleria-view — real-time WebGPU renderer.
add_executable(cleria-view apps/cleria-view/main.cpp)
# One translation unit (main.cpp #includes every view_*.inl): MSVC Debug exceeds the default
# object-file section limit (C1128) without /bigobj.
if(MSVC)
  target_compile_options(cleria-view PRIVATE /bigobj)
  # A Windows program (no console window), keeping main(): see attach_console() in main.cpp.
  set_target_properties(cleria-view PROPERTIES WIN32_EXECUTABLE TRUE)
  target_link_options(cleria-view PRIVATE /ENTRY:mainCRTStartup)
endif()
target_link_libraries(cleria-view PRIVATE cleria cleria-audio cleria-movie imgui wgpu_native sdl3)
# Player packages (release.yml): no fallback to the development checkout's assets and res/.
option(CLERIA_RELEASE "Build for a player package" OFF)
if(CLERIA_RELEASE)
  target_compile_definitions(cleria-view PRIVATE CLERIA_RELEASE)
endif()

# Copy the runtime DLLs next to the viewer so it runs from the build dir.
if(WIN32)
add_custom_command(TARGET cleria-view POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${TP}/wgpu/lib/wgpu_native.dll" "$<TARGET_FILE_DIR:cleria-view>"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${TP}/sdl3/lib/x64/SDL3.dll" "$<TARGET_FILE_DIR:cleria-view>")
else()
add_custom_command(TARGET cleria-view POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${WGPU_NATIVE_LIB}" "$<TARGET_SONAME_FILE:SDL3::SDL3-shared>" "$<TARGET_FILE_DIR:cleria-view>")
endif()
add_custom_command(TARGET cleria-view POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/res/hud_font.bin" "$<TARGET_FILE_DIR:cleria-view>"
  # the shell's fonts (Fira Sans / Fira Sans Condensed, SIL OFL; third_party/fonts)
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${TP}/fonts" "$<TARGET_FILE_DIR:cleria-view>/fonts")

# ---- tests ------------------------------------------------------------------
# The self-verifying test apps, registered so `ctest -C Release` runs the suite.
# All take the extracted asset root and exit non-zero on any failed assert. The root is
# CLERIA_TEST_ASSETS: by default CleriaCore's data folder when an unpack has completed there
# ($CLERIA_DATA, else gamedata::data_dir()'s default: %LOCALAPPDATA%/CleriaCore/data, macOS
# ~/Library/Application Support/CleriaCore/data, Linux $XDG_DATA_HOME/CleriaCore/data), else the
# legacy `assets/` in the source dir.
if(DEFINED ENV{CLERIA_DATA})
  set(_cleria_data "$ENV{CLERIA_DATA}")
elseif(WIN32)
  set(_cleria_data "$ENV{LOCALAPPDATA}/CleriaCore/data")
elseif(APPLE)
  set(_cleria_data "$ENV{HOME}/Library/Application Support/CleriaCore/data")
elseif(DEFINED ENV{XDG_DATA_HOME})
  set(_cleria_data "$ENV{XDG_DATA_HOME}/CleriaCore/data")
else()
  set(_cleria_data "$ENV{HOME}/.local/share/CleriaCore/data")
endif()
file(TO_CMAKE_PATH "${_cleria_data}" _cleria_data)
if(EXISTS "${_cleria_data}/cleria-gamedata.manifest" AND NOT EXISTS "${_cleria_data}/.cleria-installing")
  set(_cleria_assets_default "${_cleria_data}")
else()
  set(_cleria_assets_default "${CMAKE_SOURCE_DIR}/assets")
endif()
set(CLERIA_TEST_ASSETS "${_cleria_assets_default}" CACHE PATH "Asset root the ctest suite runs on")
enable_testing()
foreach(t collisiontest bodytest worldtest enemytest asotest animtest savetest combattest movetest aitest ef2simtest bullettest meshtest invtest booktest picktest wndtest bosstest boss02test boss03test boss04test boss05test boss06test boss07test boss08test camtest skilltest actiontest locustest chrtest movietest settingstest inputtest mousetest gamedatatest accesstest padtest videotest atan2ftest crtmathtest)
  add_test(NAME ${t}
           COMMAND cleria-${t} "${CLERIA_TEST_ASSETS}"
           WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endforeach()
# cotest's room-root sweep takes ~17 minutes in one process: CTest runs it as 10 shards, every room in
# exactly one (`ctest -R cotest` = all of it; by hand, `cleria-cotest <assets>` is still the whole sweep).
# 10: measured 2026-10-09, no shard is then longer than the slowest single room (S_4003, ~4 minutes).
foreach(i RANGE 9)
  add_test(NAME cotest_${i}
           COMMAND cleria-cotest "${CLERIA_TEST_ASSETS}" --shard ${i}/10
           WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  set_tests_properties(cotest_${i} PROPERTIES LABELS assets)
endforeach()
add_test(NAME modstest COMMAND cleria-modstest "${CLERIA_TEST_ASSETS}" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
add_test(NAME contenttest COMMAND cleria-contenttest "${CLERIA_TEST_ASSETS}" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
add_test(NAME xso13test COMMAND cleria-xso13test "${CLERIA_TEST_ASSETS}" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
add_test(NAME dueltest COMMAND cleria-dueltest "${CLERIA_TEST_ASSETS}" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
add_test(NAME weathertest COMMAND cleria-weathertest "${CLERIA_TEST_ASSETS}" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
# The SE voices (loop, the dup buffer pick, Se_Stop / Se_Volume, the scene entry's stop-all), no device.
add_test(NAME audiotest COMMAND cleria-audiotest "${CLERIA_TEST_ASSETS}" --voices WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(audiotest PROPERTIES LABELS assets)
# An untagged track (YSO_020, 3563136 samples) started 1 s before its end restarts from 0: the game starts every
# stream with the track loop flag (src/cleria/audio.hpp).
add_test(NAME audiotest_bgm_wrap COMMAND cleria-audiotest "${CLERIA_TEST_ASSETS}" --bgm demo6 3 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(audiotest_bgm_wrap PROPERTIES LABELS assets ENVIRONMENT "CLERIA_BGM_SEEK=3519000"
                     PASS_REGULAR_EXPRESSION "looped 1 time")
# The tests that read game data (they fail without an unpacked game). CI runs `ctest -LE assets`
# (.github/workflows/build.yml); every other test passes with no asset root (measured 2026-10-08).
set_tests_properties(worldtest enemytest asotest animtest ef2simtest bullettest invtest booktest bosstest
                     locustest xso13test dueltest weathertest PROPERTIES LABELS assets)

# ---- Speedrun mode (build/shell_re/SPEEDRUN.md): the timer, splits, LiveSplit Server client ----
add_library(cleria-speedrun STATIC src/cleria/speedrun.cpp src/cleria/livesplit.cpp)
target_link_libraries(cleria-speedrun PUBLIC cleria)
if(WIN32)
  target_link_libraries(cleria-speedrun PRIVATE ws2_32)
endif()
target_link_libraries(cleria-view PRIVATE cleria-speedrun)
# cleria-speedruntest: the timer / split logic on a fake clock, the splits file, LiveSplit over loopback.
add_executable(cleria-speedruntest apps/cleria-speedruntest/main.cpp)
target_link_libraries(cleria-speedruntest PRIVATE cleria-speedrun)
if(WIN32)
  target_link_libraries(cleria-speedruntest PRIVATE ws2_32)
endif()
add_test(NAME speedruntest COMMAND cleria-speedruntest "${CLERIA_TEST_ASSETS}" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# ---- JSON and the WebSocket (the Lua mods' cleria.json / cleria.net, build/shell_re/LUA_API.md §8) ------
# json is generic (src/cleria/json); the WinHTTP WebSocket is cleria-net. Both were written for the
# built-in Archipelago client (ARCHIPELAGO.md), removed in favour of the Lua mod samples/mods/archipelago_lua.
target_sources(cleria PRIVATE src/cleria/json.cpp)
# The C library functions as the x64 MSVC C library computes them (FMA3 CPUs), and off Windows the ones every
# program links (platform_math.cpp, msvc_*.cpp).
target_sources(cleria PRIVATE src/cleria/platform_math.cpp src/cleria/msvc_trigf.cpp src/cleria/msvc_trigd.cpp src/cleria/msvc_inv.cpp
               src/cleria/msvc_atan2.cpp src/cleria/msvc_pow.cpp src/cleria/msvc_powf.cpp src/cleria/msvc_hypotf.cpp
               src/cleria/msvc_exp_log.cpp)
add_library(cleria-net STATIC src/cleria/net/websocket.cpp src/cleria/net/socket.cpp src/cleria/modget.cpp)
target_link_libraries(cleria-net PRIVATE miniz)   # inflates permessage-deflate messages
if(WIN32)
target_link_libraries(cleria-net PUBLIC cleria winhttp ws2_32 secur32)
else()
  # Off Windows the WebSocket is libcurl's (7.86+, built with WebSockets) when this is on; off, opening
  # one reports "unsupported on this platform" through the Closed event.
  option(CLERIA_CURL "WebSocket transport (cleria.net) over libcurl (Linux / macOS)" OFF)
  target_link_libraries(cleria-net PUBLIC cleria)
  if(CLERIA_CURL)
    # third_party/curl when tools/fetch_deps.py built it (Linux: a static 8.11.1 with WebSockets), else the
    # system's libcurl.
    if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/curl/lib/cmake/CURL/CURLConfig.cmake")
      find_package(CURL CONFIG REQUIRED PATHS "${CMAKE_SOURCE_DIR}/third_party/curl" NO_DEFAULT_PATH)
    else()
      find_package(CURL REQUIRED)
    endif()
    target_compile_definitions(cleria-net PUBLIC CLERIA_CURL)
    target_link_libraries(cleria-net PRIVATE CURL::libcurl)
  endif()
endif()
# cleria-jsontest — the JSON parser / writer (src/cleria/json.hpp).
add_executable(cleria-jsontest apps/cleria-jsontest/main.cpp)
target_link_libraries(cleria-jsontest PRIVATE cleria)
# cleria-nettest — address parsing and a loopback WebSocket echo server (127.0.0.1 only; no internet).
add_executable(cleria-nettest apps/cleria-nettest/main.cpp)
target_link_libraries(cleria-nettest PRIVATE cleria-net miniz)
if(WIN32)
  target_link_libraries(cleria-nettest PRIVATE ws2_32)
endif()
add_test(NAME jsontest COMMAND cleria-jsontest WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
add_test(NAME nettest COMMAND cleria-nettest WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# ---- Lua scripting for mods (build/shell_re/LUA_API.md) ----------------------------------------------
# Lua 5.4.9 (lua.org, MIT), vendored verbatim in third_party/lua (minus the lua / luac programs). Built AS C++,
# so a Lua error is a C++ throw (ldo.c LUAI_THROW) that unwinds the C++ frames of the cleria.* API functions.
file(GLOB LUA_SRC CONFIGURE_DEPENDS "${TP}/lua/src/*.c")
list(FILTER LUA_SRC EXCLUDE REGEX "/(lua|luac)\.c$")
set_source_files_properties(${LUA_SRC} PROPERTIES LANGUAGE CXX)
add_library(lua54 STATIC ${LUA_SRC})
target_include_directories(lua54 PUBLIC "${TP}/lua/src")
if(MSVC)
  target_compile_options(lua54 PRIVATE /W0 /TP)
else()
  target_compile_options(lua54 PRIVATE -w -x c++)
endif()
# cleria-script: one sandboxed lua_State per mod with Script=, and the cleria.* API over the C++ layers.
add_library(cleria-script STATIC src/cleria/modscript.cpp src/cleria/modscript_api.cpp)
target_link_libraries(cleria-script PUBLIC cleria cleria-net lua54)
target_link_libraries(cleria-view PRIVATE cleria-script)
# cleria-luatest: sandbox escapes refused, runaway loops stopped, errors disable the mod, API round trips.
add_executable(cleria-luatest apps/cleria-luatest/main.cpp)
target_link_libraries(cleria-luatest PRIVATE cleria-script)
if(WIN32)
  target_link_libraries(cleria-luatest PRIVATE ws2_32)
endif()
add_test(NAME luatest COMMAND cleria-luatest "${CLERIA_TEST_ASSETS}" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# ---- Mod files (build/shell_re/MODFILE.md): .cleriamod install + the per-user file association ----------
target_sources(cleria PRIVATE src/cleria/modinstall.cpp src/cleria/fileassoc.cpp)
# cleria-modfiletest: package checks, installs into a scratch mods folder, the loader on .cleriamod, the
# association on a FAKE registry (the real HKCU is never opened by a test).
add_executable(cleria-modfiletest apps/cleria-modfiletest/main.cpp)
target_link_libraries(cleria-modfiletest PRIVATE cleria miniz)
add_test(NAME modfiletest COMMAND cleria-modfiletest WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# ---- The kill / boss EXP award and the combo meter (build/progress_re/REWARDS.md, audit/combat.md #28-#36) ----
target_sources(cleria PRIVATE src/cleria/exp_award.cpp)
# cleria-exptest: FUN_004fa2f0's level scaler (x0.6666), the floor / max-1 / chain bonus, FUN_004fa4a0, the meter.
add_executable(cleria-exptest apps/cleria-exptest/main.cpp)
target_link_libraries(cleria-exptest PRIVATE cleria)
add_test(NAME exptest COMMAND cleria-exptest WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
