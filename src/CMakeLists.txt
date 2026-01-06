# src/CMakeLists.txt
add_library(rbcglob
  rbcglob/rbcglob.c
  rbcglob/fnmatch.c
  rbcglob/traverse.c
  rbcglob/utils.c
  rbcglob/brace.c
  rbcglob/compiler.c
  rbcglob/arena.c
  rbcglob/join.c
  rbcglob/expand_path.c
  rbcglob/dirname.c
  rbcglob/basename.c
  rbcglob/extname.c
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
