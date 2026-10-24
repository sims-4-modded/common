# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

# The shared code of the mod loader and the mods of The Sims 4. A mod project includes this folder as a git submodule
# (external/common) with add_subdirectory. Built alone, the project builds the libraries and the tests.
#
# Each DLL that goes to the game (the extender and each DLL mod) uses the static C runtime and brings no other DLL.
# Thus every target of the projects uses the static C runtime, and vcpkg uses the triplet x64-windows-static.

cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

project(
  ts4_common
  VERSION 0.1.0
  LANGUAGES C CXX
)

if (PROJECT_IS_TOP_LEVEL)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()

option(TS4_COMMON_BUILD_TESTS "Build the tests of common" ${PROJECT_IS_TOP_LEVEL})
option(TS4_WARNINGS_AS_ERRORS "Make the compiler stop on a warning" ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompilerWarnings.cmake)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# Result: headers only.
add_library(ts4_common INTERFACE)
target_include_directories(ts4_common INTERFACE src)
target_compile_features(ts4_common INTERFACE cxx_std_20)

# The parts of the mods that do not need the game: the tests use them with fakes.
add_library(ts4_mod_core STATIC src/mod_core/GameVersion.h src/mod_core/GameVersion.cpp)
target_include_directories(ts4_mod_core PUBLIC src)
target_link_libraries(ts4_mod_core PUBLIC ts4_common)
ts4_set_warnings(ts4_mod_core)

# The interface between the extender and a DLL mod: a C header.
add_library(ts4_mod_api INTERFACE)
target_include_directories(ts4_mod_api INTERFACE src)

# spdlog with the fmt library of vcpkg. The static triplet makes both static, so no DLL goes to the game.
add_library(ts4_mod_spdlog INTERFACE)
target_link_libraries(ts4_mod_spdlog INTERFACE spdlog::spdlog)

# The C++ side of the interface in a DLL mod.
add_library(ts4_mod_sdk STATIC src/mod_sdk/ModHost.h src/mod_sdk/ModHost.cpp)
target_include_directories(ts4_mod_sdk PUBLIC src)
target_link_libraries(ts4_mod_sdk PUBLIC ts4_mod_api ts4_mod_core)
ts4_set_warnings(ts4_mod_sdk)

if (TS4_COMMON_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif ()
