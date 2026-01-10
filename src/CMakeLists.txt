# src/CMakeLists.txt
add_library(rbcglob
  # Dir class methods
  dir/glob.c
  dir/home.c

  # File class methods
  file/fnmatch.c
  file/join.c
  file/expand_path.c
  file/dirname.c
  file/basename.c
  file/extname.c

  # Internal implementation
  traverse.c
  utils.c
  arena.c

  # Pattern & Matcher implementation
  pattern/node.c
  pattern/compiler.c
  match/match.c

  # Directory Walker
  dir/walker.c
)

target_include_directories(rbcglob
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Set compile options for MSVC and others if needed
if(MSVC)
  target_compile_definitions(rbcglob PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

target_compile_options(rbcglob PRIVATE
  $<$<C_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
)

# Export and install
install(TARGETS rbcglob
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
