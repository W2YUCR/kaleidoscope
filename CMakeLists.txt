cmake_minimum_required(VERSION 3.28)

project(Kaleidoscope)

find_package(LLVM REQUIRED CONFIG)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(src)
add_subdirectory(test)
