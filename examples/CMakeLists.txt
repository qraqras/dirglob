# examples/CMakeLists.txt
add_executable(dirglob_example usage.c)

target_include_directories(dirglob_example PRIVATE "${PROJECT_SOURCE_DIR}/include")

target_link_libraries(dirglob_example PRIVATE dirglob)

install(TARGETS dirglob_example RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
