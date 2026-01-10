# src/CMakeLists.txt
add_library(rbcglob
  glob.c
  compiler.c
  walker.c
  fnmatch.c
  path.c
  utils.c
  arena.c
  matcher_exec.c
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
