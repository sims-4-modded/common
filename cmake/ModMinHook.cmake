# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

# The hooks of the extender use MinHook. The vcpkg library of MinHook is a DLL, but the extender must bring no other
# DLL into the game folder. Thus this file compiles the MinHook sources into the static library ts4_minhook, from the
# same archive as the vcpkg port (vcpkg/ports/minhook/portfile.cmake). The file makes the target only one time, so a
# project that includes common can also include this file.

if (TARGET ts4_minhook)
  return()
endif ()

set(TS4_MINHOOK_VERSION "1.3.4")
set(TS4_MINHOOK_SHA512
    "8a33233598b56ad9da44d22d470c2432f68364dac31bc719fcd6b085e681fa10ddd41865fbde056ee7f4e7a075cc135344b6bf444eadbd7e7314ee1bedfd89b5"
)
set(_ts4_minhook_archive_name "TsudaKageyu-minhook-v${TS4_MINHOOK_VERSION}.tar.gz")
set(_ts4_minhook_deps "${CMAKE_BINARY_DIR}/_deps/ts4_minhook")

# Use the archive in the download cache of vcpkg. If it is not there, download it one time into that cache, or into
# the build folder when the cache is not set.
if (DEFINED ENV{VCPKG_DOWNLOADS})
  set(_ts4_minhook_archive "$ENV{VCPKG_DOWNLOADS}/${_ts4_minhook_archive_name}")
else ()
  set(_ts4_minhook_archive "${_ts4_minhook_deps}/${_ts4_minhook_archive_name}")
endif ()
if (NOT EXISTS "${_ts4_minhook_archive}")
  message(STATUS "Download the MinHook sources ${TS4_MINHOOK_VERSION}")
  file(
    DOWNLOAD "https://github.com/TsudaKageyu/minhook/archive/v${TS4_MINHOOK_VERSION}.tar.gz" "${_ts4_minhook_archive}"
    EXPECTED_HASH SHA512=${TS4_MINHOOK_SHA512}
    STATUS _ts4_minhook_status
  )
  list(GET _ts4_minhook_status 0 _ts4_minhook_code)
  if (NOT _ts4_minhook_code EQUAL 0)
    file(REMOVE "${_ts4_minhook_archive}")
    message(FATAL_ERROR "The download of the MinHook sources failed: ${_ts4_minhook_status}")
  endif ()
endif ()
file(SHA512 "${_ts4_minhook_archive}" _ts4_minhook_hash)
if (NOT _ts4_minhook_hash STREQUAL TS4_MINHOOK_SHA512)
  message(FATAL_ERROR "${_ts4_minhook_archive} is not the archive of MinHook ${TS4_MINHOOK_VERSION}")
endif ()

set(TS4_MINHOOK_DIR "${_ts4_minhook_deps}/minhook-${TS4_MINHOOK_VERSION}")
if (NOT EXISTS "${TS4_MINHOOK_DIR}/src/hook.c")
  file(ARCHIVE_EXTRACT INPUT "${_ts4_minhook_archive}" DESTINATION "${_ts4_minhook_deps}")
endif ()

add_library(
  ts4_minhook STATIC
  ${TS4_MINHOOK_DIR}/src/buffer.c
  ${TS4_MINHOOK_DIR}/src/hook.c
  ${TS4_MINHOOK_DIR}/src/trampoline.c
  ${TS4_MINHOOK_DIR}/src/hde/hde64.c
)
set_target_properties(ts4_minhook PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# The include folder comes first, so that no other MinHook header can take its place.
target_include_directories(ts4_minhook BEFORE PUBLIC ${TS4_MINHOOK_DIR}/include)
# The MinHook sources are not ours. The compiler gives no warnings for them.
target_compile_options(ts4_minhook PRIVATE /W0)
