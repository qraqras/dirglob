# src/CMakeLists.txt
add_library(rbc
  glob.c           # v1 API with v2 optimizations integrated
  walker.c         # Unified walker (recursive, MRI-compatible)
  plan.c           # Multi-pattern execution plan
  fnmatch.c
  utils.c
  arena.c
  brace.c
  str_list.c
  # v2 optimization modules (used by glob.c)
  glob_v2_hints.c      # Hint generation
  glob_v2_brace.c      # Brace expansion optimization
  glob_v2_simple.c     # Simple pattern optimization (*.c, etc.)
  string_set.c
)

target_include_directories(rbc
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Set compile options for MSVC and others if needed
if(MSVC)
  target_compile_definitions(rbc PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

target_compile_options(rbc PRIVATE
  $<$<C_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
)

# Export and install
install(TARGETS rbc
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
