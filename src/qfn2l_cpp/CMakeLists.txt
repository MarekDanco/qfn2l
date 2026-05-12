cmake_minimum_required(VERSION 3.16)
project(qfn2l_cpp CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Boost ───────────────────────────────────────────────────────────────────
# boost::multiprecision::cpp_int is header-only, but require the headers at
# configure time so missing dependencies fail clearly.
find_package(Boost REQUIRED)

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
set(CORE_SOURCES
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
)

add_library(qfn2l_core STATIC ${CORE_SOURCES})

function(configure_qfn2l_target target)
  target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(${target} PRIVATE smt-switch::smt-switch)
  if(TARGET Boost::boost)
    target_link_libraries(${target} PRIVATE Boost::boost)
  else()
    target_include_directories(${target} PRIVATE ${Boost_INCLUDE_DIRS})
  endif()

  if(BACKEND_Z3)
    target_compile_definitions(${target} PRIVATE BACKEND_Z3)
    # Z3 backend headers (z3_solver.h, z3_term.h, z3++.h).
    target_include_directories(${target} PRIVATE
      "${SMT_SWITCH_DIR}/../z3/include"
      "${SMT_SWITCH_DIR}/../deps/z3/src/api/c++"
      "${SMT_SWITCH_DIR}/../deps/z3/src/api"
    )
    if(Z3_LIB)
      target_link_libraries(${target} PRIVATE "${Z3_LIB}")
    endif()
  endif()

  if(BACKEND_CVC5)
    target_compile_definitions(${target} PRIVATE BACKEND_CVC5)
    if(CVC5_LIB)
      target_link_libraries(${target} PRIVATE "${CVC5_LIB}")
    endif()
  endif()

  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
endfunction()

if(BACKEND_Z3)
  find_library(Z3_LIB z3
    PATHS "${SMT_SWITCH_DIR}/../deps/z3/build" "${SMT_SWITCH_DIR}/z3"
    NO_DEFAULT_PATH)
  if(NOT Z3_LIB)
    find_library(Z3_LIB z3)
  endif()
endif()
if(BACKEND_CVC5)
  find_library(CVC5_LIB cvc5)
endif()

configure_qfn2l_target(qfn2l_core)

add_executable(qfn2l main.cpp)
target_link_libraries(qfn2l PRIVATE qfn2l_core)
configure_qfn2l_target(qfn2l)

include(CTest)
if(BUILD_TESTING)
  add_executable(qfn2l_numeric_tests tests/test_numerics.cpp)
  target_link_libraries(qfn2l_numeric_tests PRIVATE qfn2l_core)
  configure_qfn2l_target(qfn2l_numeric_tests)
  add_test(NAME qfn2l_numeric_tests COMMAND qfn2l_numeric_tests)
endif()
