# examples/CMakeLists.txt
add_executable(rbc_example usage.c)

target_include_directories(rbc_example PRIVATE "${PROJECT_SOURCE_DIR}/include")

target_link_libraries(rbc_example PRIVATE rbc)

install(TARGETS rbc_example RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# bench_glob uses POSIX glob.h, only build on non-Windows
if(NOT WIN32)
  add_executable(bench_glob bench_glob.c)
  target_include_directories(bench_glob PRIVATE "${PROJECT_SOURCE_DIR}/include")
  target_link_libraries(bench_glob PRIVATE rbc)
endif()

add_executable(example_glob example_glob.c)
target_include_directories(example_glob PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(example_glob PRIVATE rbc)
