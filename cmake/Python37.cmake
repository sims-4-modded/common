# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

# The tests that use Python 3.7 find its folder here, because the game runs Python 3.7. The environment variable
# TS4_PYTHON37 gives the folder. If it is not set, the file asks the Python launcher (py -3.7). The result is the cache
# variable TS4_PYTHON37_DIR, which is empty if this file finds no Python 3.7. The file sets it only one time, so a
# project that includes common can also include this file.

if (DEFINED TS4_PYTHON37_DIR)
  return()
endif ()

set(_ts4_python37_dir "")
if (DEFINED ENV{TS4_PYTHON37})
  file(TO_CMAKE_PATH "$ENV{TS4_PYTHON37}" _ts4_python37_dir)
else ()
  find_program(_ts4_py_launcher NAMES py)
  if (_ts4_py_launcher)
    execute_process(
      COMMAND "${_ts4_py_launcher}" -3.7 -c "import sys; print(sys.base_prefix)"
      OUTPUT_VARIABLE _ts4_python37_out
      OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE _ts4_python37_result
      ERROR_QUIET
    )
    if (_ts4_python37_result EQUAL 0)
      file(TO_CMAKE_PATH "${_ts4_python37_out}" _ts4_python37_dir)
    endif ()
  endif ()
  unset(_ts4_py_launcher CACHE)
endif ()
if (_ts4_python37_dir AND NOT EXISTS "${_ts4_python37_dir}/python.exe")
  message(STATUS "The folder ${_ts4_python37_dir} has no python.exe")
  set(_ts4_python37_dir "")
endif ()
set(TS4_PYTHON37_DIR "${_ts4_python37_dir}" CACHE PATH "The folder of Python 3.7 for the tests")
