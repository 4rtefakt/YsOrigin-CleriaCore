# cleria-modkit — the mod layer: mod packages and their load order (mods, modinstall, fileassoc), the file
# overrides (vfs), content/*.ini (content, sidecar, inidoc, items), the event bus (mod_events) and the Lua
# scripts with their cleria.* API (modscript, json, net/websocket, net/socket). It reaches a game only through
# cleria::script::Host and depends on nothing else of the engine, so it builds alone, 32-bit included.
#
# The root CMakeLists.txt includes this file. Another host of the same mods includes it too, after
# setting the C++ standard (20); TP may be set first to point at another third_party folder.
set(CLERIA_MODKIT_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
if(NOT DEFINED TP)
  set(TP "${CLERIA_MODKIT_ROOT}/third_party")
endif()

# miniz 3.0.2 (richgel999/miniz, MIT), vendored verbatim in third_party/miniz: inflate + crc32
# for the game's NI/NA archives (src/cleria/ni_archive). Own target so its warnings stay out of /W4.
add_library(miniz STATIC "${TP}/miniz/miniz.c")
target_include_directories(miniz PUBLIC "${TP}/miniz")
if(MSVC)
  target_compile_options(miniz PRIVATE /W0)
else()
  target_compile_options(miniz PRIVATE -w)
endif()

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

set(_modkit_src "${CLERIA_MODKIT_ROOT}/src/cleria")
add_library(cleria-modkit STATIC
  ${_modkit_src}/ini.cpp
  ${_modkit_src}/inidoc.cpp
  ${_modkit_src}/vfs.cpp
  ${_modkit_src}/mods.cpp
  ${_modkit_src}/modinstall.cpp
  ${_modkit_src}/fileassoc.cpp
  ${_modkit_src}/items.cpp
  ${_modkit_src}/sidecar.cpp
  ${_modkit_src}/mod_events.cpp
  ${_modkit_src}/content.cpp
  ${_modkit_src}/json.cpp
  ${_modkit_src}/net/websocket.cpp
  ${_modkit_src}/net/socket.cpp
  ${_modkit_src}/modscript.cpp
  ${_modkit_src}/modscript_api.cpp)
target_include_directories(cleria-modkit PUBLIC "${CLERIA_MODKIT_ROOT}/src")
target_link_libraries(cleria-modkit PUBLIC lua54 PRIVATE miniz)
if(WIN32)
  target_link_libraries(cleria-modkit PUBLIC winhttp ws2_32)
else()
  # Off Windows the WebSocket is libcurl's (7.86+, built with WebSockets) when this is on; off, opening
  # one reports "unsupported on this platform" through the Closed event.
  option(CLERIA_CURL "WebSocket transport (cleria.net) over libcurl (Linux / macOS)" OFF)
  if(CLERIA_CURL)
    # third_party/curl when tools/fetch_deps.py built it (Linux: a static 8.11.1 with WebSockets), else the
    # system's libcurl.
    if(EXISTS "${TP}/curl/lib/cmake/CURL/CURLConfig.cmake")
      find_package(CURL CONFIG REQUIRED PATHS "${TP}/curl" NO_DEFAULT_PATH)
    else()
      find_package(CURL REQUIRED)
    endif()
    target_compile_definitions(cleria-modkit PUBLIC CLERIA_CURL)
    target_link_libraries(cleria-modkit PRIVATE CURL::libcurl)
  endif()
endif()
