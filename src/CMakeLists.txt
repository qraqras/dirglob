# src/CMakeLists.txt
#
# Directory structure:
#   src/
#   ├── core/       - Core glob/fnmatch implementation
#   ├── optimizers/ - v2 pattern-specific optimizations
#   └── utils/      - Utility modules (arena, string helpers)
#

add_library(rbc
  # Core modules
  core/glob.c           # Main API (v1 + v2 integration)
  core/fnmatch.c        # Pattern matching
  core/plan.c           # Multi-pattern execution plan
  core/brace.c          # Brace expansion

  # v2 Optimizers (pattern-specific fast paths)
  optimizers/glob_v2_hints.c      # Hint generation
  optimizers/glob_v2_brace.c      # Brace expansion optimization
  optimizers/glob_v2_simple.c     # Simple/Multi-segment pattern optimization
  optimizers/glob_v2_recursive.c  # Recursive (**) optimization

  # Utilities
  utils/arena.c         # Memory arena
  utils/utils.c         # Helper functions
  utils/str_list.c      # String list
  utils/string_set.c    # String hash set
)

target_include_directories(rbc
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/utils
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
