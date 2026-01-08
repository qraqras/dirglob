# examples/CMakeLists.txt
add_executable(rbcglob_example usage.c)

target_include_directories(rbcglob_example PRIVATE "${PROJECT_SOURCE_DIR}/include")

target_link_libraries(rbcglob_example PRIVATE rbcglob)

install(TARGETS rbcglob_example RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(bench_glob bench_glob.c)
target_include_directories(bench_glob PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(bench_glob PRIVATE rbcglob)
