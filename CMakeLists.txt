cmake_minimum_required(VERSION 3.15)
project(dirglob LANGUAGES C)

# Options
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(BUILD_EXAMPLES "Build example programs" ON)
option(BUILD_TESTING "Build tests" ON)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Library
add_subdirectory(src)

# Examples
if(BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

# Tests
if(BUILD_TESTING)
  include(CTest)
  enable_testing()
  add_subdirectory(tests)
endif()

# Install
include(GNUInstallDirs)
install(TARGETS dirglob
  EXPORT dirglobTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Export config
install(EXPORT dirglobTargets FILE dirglobTargets.cmake NAMESPACE dirglob:: DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/dirglob/cmake)

# Package config (basic)
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/dirglobConfigVersion.cmake"
  VERSION 0.1.0
  COMPATIBILITY AnyNewerVersion
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/dirglobConfigVersion.cmake" DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/dirglob/cmake)
