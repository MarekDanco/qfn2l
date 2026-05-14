cmake_minimum_required(VERSION 3.16)
project(qfn2l_cppz3 CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Boost ────────────────────────────────────────────────────────────────────
# boost::multiprecision::cpp_int is header-only.
find_package(Boost REQUIRED)

# ── Z3 ───────────────────────────────────────────────────────────────────────
# Try cmake config first (works when z3 is installed system-wide or via vcpkg).
find_package(Z3 QUIET CONFIG)
if(Z3_FOUND)
  # Modern z3 cmake config exports z3::libz3
  if(NOT TARGET z3::libz3)
    add_library(z3::libz3 UNKNOWN IMPORTED)
    set_target_properties(z3::libz3 PROPERTIES
      IMPORTED_LOCATION "${Z3_LIBRARIES}"
      INTERFACE_INCLUDE_DIRECTORIES "${Z3_INCLUDE_DIRS}")
  endif()
else()
  # Fallback: locate headers and library manually.
  # Typical system install: /usr/include/z3/ and libz3.so
  # Local build: set Z3_ROOT or Z3_DIR when invoking cmake.
  find_path(Z3_INCLUDE_DIR z3++.h
    PATHS "${Z3_ROOT}/include" /usr/include/z3 /usr/local/include/z3)
  find_library(Z3_LIBRARY z3
    PATHS "${Z3_ROOT}/lib" /usr/lib /usr/local/lib)

  if(NOT Z3_INCLUDE_DIR OR NOT Z3_LIBRARY)
    message(FATAL_ERROR
      "z3 not found. Install z3 (e.g. 'apt install libz3-dev') or set Z3_ROOT.")
  endif()

  add_library(z3::libz3 UNKNOWN IMPORTED)
  set_target_properties(z3::libz3 PROPERTIES
    IMPORTED_LOCATION "${Z3_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${Z3_INCLUDE_DIR}")
endif()

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

add_library(qfn2l_cppz3_core STATIC ${CORE_SOURCES})

function(configure_qfn2l_target target)
  target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(${target} PRIVATE z3::libz3)
  if(TARGET Boost::boost)
    target_link_libraries(${target} PRIVATE Boost::boost)
  else()
    target_include_directories(${target} PRIVATE ${Boost_INCLUDE_DIRS})
  endif()
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
endfunction()

configure_qfn2l_target(qfn2l_cppz3_core)

add_executable(qfn2l_cppz3 main.cpp)
target_link_libraries(qfn2l_cppz3 PRIVATE qfn2l_cppz3_core)
configure_qfn2l_target(qfn2l_cppz3)

# ── Tests ────────────────────────────────────────────────────────────────────
include(CTest)
if(BUILD_TESTING)
  set(TEST_TARGETS
    qfn2l_cppz3_numeric_tests
    qfn2l_cppz3_solver_smoke_tests
    qfn2l_cppz3_visitors_tests
    qfn2l_cppz3_pures_tests
    qfn2l_cppz3_projections_tests
  )

  add_executable(qfn2l_cppz3_numeric_tests     tests/test_numerics.cpp)
  add_executable(qfn2l_cppz3_solver_smoke_tests tests/test_solver_smoke.cpp)
  add_executable(qfn2l_cppz3_visitors_tests     tests/test_visitors.cpp)
  add_executable(qfn2l_cppz3_pures_tests        tests/test_pures.cpp)
  add_executable(qfn2l_cppz3_projections_tests  tests/test_projections.cpp)

  foreach(test_target IN LISTS TEST_TARGETS)
    target_link_libraries(${test_target} PRIVATE qfn2l_cppz3_core)
    configure_qfn2l_target(${test_target})
    add_test(NAME ${test_target} COMMAND ${test_target})
  endforeach()

  add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS ${TEST_TARGETS}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  )
endif()
