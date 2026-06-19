cmake_minimum_required(VERSION 3.14)
project(maplibre_windows LANGUAGES CXX)

set(PLUGIN_NAME "maplibre_windows_plugin")
cmake_policy(VERSION 3.14...3.25)

# Locate maplibre-native and its renderer build dir. Prefer the ANGLE/EGL build
# (zero-copy GPU surface); fall back to the desktop-GL build.
set(_maplibre_build_candidates "build-windows-egl" "build-windows-opengl")
set(_maplibre_search_dir "${CMAKE_CURRENT_SOURCE_DIR}")
set(MAPLIBRE_NATIVE_DIR "")
set(MAPLIBRE_BUILD_NAME "")
foreach(_i RANGE 12)
  foreach(_cand IN LISTS _maplibre_build_candidates)
    file(TO_CMAKE_PATH "${_maplibre_search_dir}/maplibre-native/${_cand}/mbgl-core-deps.txt" _deps_file)
    if(EXISTS "${_deps_file}")
      file(TO_CMAKE_PATH "${_maplibre_search_dir}/maplibre-native" MAPLIBRE_NATIVE_DIR)
      set(MAPLIBRE_BUILD_NAME "${_cand}")
      break()
    endif()
  endforeach()
  if(NOT MAPLIBRE_NATIVE_DIR STREQUAL "")
    break()
  endif()
  get_filename_component(_maplibre_search_dir "${_maplibre_search_dir}/.." ABSOLUTE)
endforeach()

if(MAPLIBRE_NATIVE_DIR STREQUAL "")
  message(FATAL_ERROR
    "Could not locate maplibre-native with build-windows-egl or build-windows-opengl mbgl-core-deps.txt. "
    "Build maplibre-native first: packages/maplibre_windows/scripts/build_native.ps1")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  message(FATAL_ERROR
    "maplibre_windows links Release maplibre-native libraries. "
    "Use: flutter build windows --release (or flutter run --release)")
endif()

set(MAPLIBRE_BUILD_DIR "${MAPLIBRE_NATIVE_DIR}/${MAPLIBRE_BUILD_NAME}")
set(MAPLIBRE_EMBED_LIB "${MAPLIBRE_BUILD_DIR}/platform/windows/maplibre_embed/maplibre-embed.lib")

if(NOT EXISTS "${MAPLIBRE_EMBED_LIB}")
  message(FATAL_ERROR "maplibre-embed.lib not found at ${MAPLIBRE_EMBED_LIB}")
endif()

message(STATUS "maplibre_windows: linking native build '${MAPLIBRE_BUILD_NAME}'")

file(READ "${MAPLIBRE_BUILD_DIR}/mbgl-core-deps.txt" _mln_deps_raw)
string(REGEX MATCHALL "[^ ]+\\.lib" _mln_static_libs "${_mln_deps_raw}")

add_library(${PLUGIN_NAME} SHARED
  "maplibre_windows_plugin.cpp"
  "maplibre_windows_plugin_c_api.cpp"
  "map_texture.cpp"
  "map_gpu_texture.cpp"
  "crash_logger.cpp"
)

apply_standard_settings(${PLUGIN_NAME})
set_target_properties(${PLUGIN_NAME} PROPERTIES CXX_VISIBILITY_PRESET hidden)
target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)

# Emit a PDB for the plugin DLL (embeds the /Z7 debug info from the static
# maplibre-embed / mbgl-core libs) so the crash logger can resolve mbgl frames.
# Optimization is unaffected; this only adds symbols.
if(MSVC)
  target_compile_options(${PLUGIN_NAME} PRIVATE /Z7)
  target_link_options(${PLUGIN_NAME} PRIVATE /DEBUG)
endif()

target_include_directories(${PLUGIN_NAME} PRIVATE
  "${MAPLIBRE_NATIVE_DIR}/platform/windows/maplibre_embed"
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_directories(${PLUGIN_NAME} PRIVATE
  "${MAPLIBRE_BUILD_DIR}"
  "${MAPLIBRE_BUILD_DIR}/vendor/maplibre-tile-spec/cpp"
)

target_link_libraries(${PLUGIN_NAME} PRIVATE
  flutter
  flutter_wrapper_plugin
  "${MAPLIBRE_EMBED_LIB}"
  ${_mln_static_libs}
  opengl32
  d3d11
  dxgi
  dbghelp
)

target_include_directories(${PLUGIN_NAME} INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(${PLUGIN_NAME} INTERFACE flutter flutter_wrapper_plugin)

set(maplibre_windows_bundled_libraries "" PARENT_SCOPE)
