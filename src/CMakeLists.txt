# src/CMakeLists.txt
add_library(rbc
  glob.c
  walker.c
  fnmatch.c
  # fnmatch_streaming.c  # TODO: implement
  utils.c
  arena.c
  matcher.c
  brace.c
  str_list.c
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
