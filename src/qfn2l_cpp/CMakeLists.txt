cmake_minimum_required(VERSION 3.16)
project(qfn2l_cpp CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── smt-switch ──────────────────────────────────────────────────────────────
# Clone smt-switch into deps/smt-switch and configure it before running cmake.
# Typical setup:
#   git submodule add https://github.com/stanford-centaur/smt-switch deps/smt-switch
#   cd deps/smt-switch && ./contrib/setup-z3.sh && mkdir build && cd build
#   cmake .. -DSMT_SWITCH_LIB_ONLY=ON -Dz3=ON && make -j4 && sudo make install
#
# Or point CMAKE_PREFIX_PATH at a local installation:
#   cmake .. -DCMAKE_PREFIX_PATH=/path/to/smt-switch-install

find_package(smt-switch QUIET)
if(NOT smt-switch_FOUND)
  # Fallback: look for a local build in deps/
  set(SMT_SWITCH_DIR "${CMAKE_SOURCE_DIR}/../../deps/smt-switch/build"
      CACHE PATH "Path to smt-switch build directory")
  find_library(SMT_SWITCH_LIB smt-switch PATHS "${SMT_SWITCH_DIR}" NO_DEFAULT_PATH)
  find_library(SMT_SWITCH_Z3_LIB smt-switch-z3
    PATHS "${SMT_SWITCH_DIR}" "${SMT_SWITCH_DIR}/z3" NO_DEFAULT_PATH)
  find_path(SMT_SWITCH_INCLUDE smt.h
    PATHS "${SMT_SWITCH_DIR}/../src" "${SMT_SWITCH_DIR}/../include" NO_DEFAULT_PATH)
  if(NOT SMT_SWITCH_LIB)
    message(FATAL_ERROR
      "smt-switch not found. Set SMT_SWITCH_DIR or install smt-switch system-wide.")
  endif()
  add_library(smt-switch::smt-switch INTERFACE IMPORTED)
  target_include_directories(smt-switch::smt-switch INTERFACE "${SMT_SWITCH_INCLUDE}")
  target_link_libraries(smt-switch::smt-switch INTERFACE "${SMT_SWITCH_LIB}")
  if(SMT_SWITCH_Z3_LIB)
    target_link_libraries(smt-switch::smt-switch INTERFACE "${SMT_SWITCH_Z3_LIB}")
  endif()
endif()

# ── Backend selection ────────────────────────────────────────────────────────
option(BACKEND_Z3   "Build with z3 backend"   ON)
option(BACKEND_CVC5 "Build with CVC5 backend" OFF)

# ── Sources ──────────────────────────────────────────────────────────────────
set(SOURCES
  tagged_logging.cpp
  stats.cpp
  utils.cpp
  visitors.cpp
  converter.cpp
  prefix.cpp
  level_info.cpp
  pures.cpp
  projections.cpp
  lia_abstraction.cpp
  qf_solver.cpp
  main.cpp
)

add_executable(qfn2l ${SOURCES})
target_include_directories(qfn2l PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(qfn2l PRIVATE smt-switch::smt-switch)

if(BACKEND_Z3)
  target_compile_definitions(qfn2l PRIVATE BACKEND_Z3)
  # Z3 backend headers (z3_solver.h, z3_term.h, z3++.h).
  target_include_directories(qfn2l PRIVATE
    "${SMT_SWITCH_DIR}/../z3/include"
    "${SMT_SWITCH_DIR}/../deps/z3/src/api/c++"
    "${SMT_SWITCH_DIR}/../deps/z3/src/api"
  )
  find_library(Z3_LIB z3
    PATHS "${SMT_SWITCH_DIR}/../deps/z3/build" "${SMT_SWITCH_DIR}/z3"
    NO_DEFAULT_PATH)
  if(NOT Z3_LIB)
    find_library(Z3_LIB z3)
  endif()
  if(Z3_LIB)
    target_link_libraries(qfn2l PRIVATE "${Z3_LIB}")
  endif()
endif()
if(BACKEND_CVC5)
  target_compile_definitions(qfn2l PRIVATE BACKEND_CVC5)
  find_library(CVC5_LIB cvc5)
  if(CVC5_LIB)
    target_link_libraries(qfn2l PRIVATE "${CVC5_LIB}")
  endif()
endif()

target_compile_options(qfn2l PRIVATE -Wall -Wextra -Wpedantic)
