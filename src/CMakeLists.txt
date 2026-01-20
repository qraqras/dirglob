# src/CMakeLists.txt
#
# Directory structure:
#   src/
#   ├── core/       - Core glob/fnmatch implementation
#   ├── optimizers/ - Pattern-specific optimizations
#   └── utils/      - Utility modules (arena, string helpers)
#

add_library(rbc
  # Core modules
  core/glob.c           # Main API
  core/fnmatch.c        # Pattern matching
  core/plan.c           # Multi-pattern execution plan
  core/trie.c           # Complete trie-based multi-pattern optimization
  core/brace.c          # Brace expansion

  # Optimizers (pattern-specific fast paths)
  optimizers/glob_hints.c      # Hint generation
  optimizers/glob_brace.c      # Brace expansion optimization
  optimizers/glob_simple.c     # Simple/Multi-segment pattern optimization
  optimizers/glob_recursive.c  # Recursive (**) optimization

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
