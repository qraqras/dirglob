# src/CMakeLists.txt
add_library(dirglob
  dirglob/dirglob.c
)

target_include_directories(dirglob
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Set compile options for MSVC and others if needed
if(MSVC)
  target_compile_definitions(dirglob PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

target_compile_options(dirglob PRIVATE
  $<$<C_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
)

# Export and install
install(TARGETS dirglob
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
