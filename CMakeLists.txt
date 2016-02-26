#cmake_minimum_required(VERSION 3.1 FATAL_ERROR)

include(cmake/Library.cmake)

set(VERBOSE 1)

# Tell Cmake where to find Find<package>.cmake files
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake" "${CMAKE_MODULE_PATH}")

#Locate papillon
find_package(Papillon REQUIRED)
papillon_print_variables_with_prefix("PAPILLON")

add_subdirectory(video)

