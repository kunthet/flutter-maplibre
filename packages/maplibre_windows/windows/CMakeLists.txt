cmake_minimum_required(VERSION 3.14)
project(maplibre_windows LANGUAGES CXX)

set(PLUGIN_NAME "maplibre_windows_plugin")
cmake_policy(VERSION 3.14...3.25)

set(_maplibre_search_dir "${CMAKE_CURRENT_SOURCE_DIR}")
set(MAPLIBRE_NATIVE_DIR "")
foreach(_i RANGE 12)
  file(TO_CMAKE_PATH "${_maplibre_search_dir}/maplibre-native/build-windows-opengl/mbgl-core-deps.txt" _deps_file)
  if(EXISTS "${_deps_file}")
    file(TO_CMAKE_PATH "${_maplibre_search_dir}/maplibre-native" MAPLIBRE_NATIVE_DIR)
    break()
  endif()
  get_filename_component(_maplibre_search_dir "${_maplibre_search_dir}/.." ABSOLUTE)
endforeach()

if(MAPLIBRE_NATIVE_DIR STREQUAL "")
  message(FATAL_ERROR
    "Could not locate maplibre-native with build-windows-opengl/mbgl-core-deps.txt. "
    "Build maplibre-native first: packages/maplibre_windows/scripts/build_native.ps1")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  message(FATAL_ERROR
    "maplibre_windows links Release maplibre-native libraries. "
    "Use: flutter build windows --release (or flutter run --release)")
endif()

set(MAPLIBRE_BUILD_DIR "${MAPLIBRE_NATIVE_DIR}/build-windows-opengl")
set(MAPLIBRE_EMBED_LIB "${MAPLIBRE_BUILD_DIR}/platform/windows/maplibre_embed/maplibre-embed.lib")

if(NOT EXISTS "${MAPLIBRE_EMBED_LIB}")
  message(FATAL_ERROR "maplibre-embed.lib not found at ${MAPLIBRE_EMBED_LIB}")
endif()

file(READ "${MAPLIBRE_BUILD_DIR}/mbgl-core-deps.txt" _mln_deps_raw)
string(REGEX MATCHALL "[^ ]+\\.lib" _mln_static_libs "${_mln_deps_raw}")

add_library(${PLUGIN_NAME} SHARED
  "maplibre_windows_plugin.cpp"
  "maplibre_windows_plugin_c_api.cpp"
  "map_texture.cpp"
)

apply_standard_settings(${PLUGIN_NAME})
set_target_properties(${PLUGIN_NAME} PROPERTIES CXX_VISIBILITY_PRESET hidden)
target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)

target_include_directories(${PLUGIN_NAME} PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${MAPLIBRE_NATIVE_DIR}/platform/windows/maplibre_embed"
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
)

target_include_directories(${PLUGIN_NAME} INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(${PLUGIN_NAME} INTERFACE flutter flutter_wrapper_plugin)

set(maplibre_windows_bundled_libraries "" PARENT_SCOPE)
