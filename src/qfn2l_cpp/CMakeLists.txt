cmake_minimum_required(VERSION 3.16)
project(qfn2l_cpp CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
  # Fallback: look for a local build in deps/ (relative to this CMakeLists.txt)
  set(SMT_SWITCH_DIR "${CMAKE_SOURCE_DIR}/deps/smt-switch/build"
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
    target_include_directories(${target} SYSTEM PRIVATE
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
  set(TEST_TARGETS
    qfn2l_numeric_tests
    qfn2l_solver_smoke_tests
    qfn2l_visitors_tests
    qfn2l_pures_tests
    qfn2l_projections_tests
  )

  add_executable(qfn2l_numeric_tests tests/test_numerics.cpp)
  add_executable(qfn2l_solver_smoke_tests tests/test_solver_smoke.cpp)
  add_executable(qfn2l_smt_switch_identity_tests tests/test_smt_switch_identity.cpp)
  add_executable(qfn2l_visitors_tests tests/test_visitors.cpp)
  add_executable(qfn2l_pures_tests tests/test_pures.cpp)
  add_executable(qfn2l_projections_tests tests/test_projections.cpp)

  foreach(test_target IN LISTS TEST_TARGETS)
    target_link_libraries(${test_target} PRIVATE qfn2l_core)
    configure_qfn2l_target(${test_target})
    add_test(NAME ${test_target} COMMAND ${test_target})
  endforeach()

  # Reproducer for an upstream smt-switch/Z3 term identity bug.  It is kept
  # buildable, but excluded from the default check target until upstream fixes
  # the expected failure: `build/qfn2l_smt_switch_identity_tests`.
  target_link_libraries(qfn2l_smt_switch_identity_tests PRIVATE qfn2l_core)
  configure_qfn2l_target(qfn2l_smt_switch_identity_tests)

  # 'make check' builds and runs tests (unlike 'make test' which skips the build).
  add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS ${TEST_TARGETS}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  )
endif()
