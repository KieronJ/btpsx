cmake_minimum_required(VERSION 3.8 FATAL_ERROR)
project(btpsx VERSION 0.1.0 LANGUAGES CXX)

LIST(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_subdirectory(external)
add_subdirectory(src)
