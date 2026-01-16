# Minimal Unity-only tests CMake
include(FetchContent)

find_program(GIT_EXE git)
if(NOT GIT_EXE)
  message(FATAL_ERROR "Git is required to fetch Unity for tests. Please install git to build tests.")
endif()

FetchContent_Declare(
  unity
  GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
  GIT_TAG v2.5.2
)
FetchContent_MakeAvailable(unity)

# If the Unity project didn't create a target we can provide a simple one
if(TARGET unity OR TARGET Unity)
  # Prefer lowercase 'unity' if available; otherwise use 'Unity'. Use a generator expression
  # so the link line is evaluated correctly at generate/build time.
  set(UNITY_TARGET "$<IF:$<TARGET_EXISTS:unity>,unity,$<IF:$<TARGET_EXISTS:Unity>,Unity,unity>>")
else()
  FetchContent_GetProperties(unity)
  if(NOT unity_POPULATED)
    FetchContent_Populate(unity)
  endif()
  add_library(unity STATIC "${unity_SOURCE_DIR}/src/unity.c")
  target_include_directories(unity PUBLIC "${unity_SOURCE_DIR}/src")
  set(UNITY_TARGET unity)
endif()

# Gather test source files (manual test files only, not generated)
file(GLOB TEST_IMPLS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS test_*.c)
list(FILTER TEST_IMPLS EXCLUDE REGEX "^test_parity_.*\\.c$")
list(FILTER TEST_IMPLS EXCLUDE REGEX "^test_glob_generated\\.c$")

# Add test helpers
list(APPEND TEST_IMPLS test_helpers.c)

# Add generated parity tests if they exist
set(GENERATED_COMMON_TEST ${CMAKE_CURRENT_BINARY_DIR}/generated/test_parity_common.c)
set(GENERATED_LINUX_TEST ${CMAKE_CURRENT_BINARY_DIR}/generated/test_parity_linux.c)
if(EXISTS ${GENERATED_COMMON_TEST})
  list(APPEND TEST_IMPLS ${GENERATED_COMMON_TEST})
endif()
if(UNIX AND NOT APPLE AND EXISTS ${GENERATED_LINUX_TEST})
  list(APPEND TEST_IMPLS ${GENERATED_LINUX_TEST})
endif()

set(GENERATED_RUNNER ${CMAKE_CURRENT_BINARY_DIR}/test_runner_generated.c)
find_program(PYTHON_EXECUTABLE python3 python)

if(NOT PYTHON_EXECUTABLE)
  message(FATAL_ERROR "Python 3 is required to generate the Unity test runner. Install python3 or set PYTHON_EXECUTABLE.")
endif()

add_custom_command(
  OUTPUT ${GENERATED_RUNNER}
  COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_unity_runner.py ${CMAKE_CURRENT_SOURCE_DIR} ${GENERATED_RUNNER}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_unity_runner.py
  COMMENT "Generating Unity test runner (Python)"
  VERBATIM
)
add_custom_target(gen_unity_runner DEPENDS ${GENERATED_RUNNER})
set(TEST_RUNNER ${GENERATED_RUNNER})

add_executable(test_dirglob ${TEST_RUNNER} ${TEST_IMPLS})
add_dependencies(test_dirglob gen_unity_runner)

target_include_directories(test_dirglob PRIVATE
  "${PROJECT_SOURCE_DIR}/include"
  "${PROJECT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(test_dirglob PRIVATE rbc ${UNITY_TARGET})

enable_testing()
add_test(NAME dirglob_test COMMAND test_dirglob WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tests/fixtures)

# Generated compatibility test (test_glob_generated.c)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test_glob_generated.c)
  add_executable(test_glob_generated
    test_glob_generated.c
    test_helpers.c
  )
  target_include_directories(test_glob_generated PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/src"
  )
  target_link_libraries(test_glob_generated PRIVATE rbc ${UNITY_TARGET})
  add_test(NAME glob_ruby_compat COMMAND test_glob_generated WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tests/fixtures)
endif()
