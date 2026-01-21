# src/CMakeLists.txt
#
# Directory structure:
#   src/
#   ├── core/  - Core glob/fnmatch/trie implementation
#   └── utils/ - Utility modules (arena, string helpers)
#

add_library(rbc
  # Core modules
  core/glob.c           # Main API
  core/fnmatch.c        # Pattern matching

  # Utilities
  utils/arena.c         # Memory arena
  utils/utils.c         # Helper functions
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
