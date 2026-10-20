# An Ovis program that uses a C library of the project's own, built with
# CMake. With ovis installed (cmake --install BUILD --component ovis --prefix
# PREFIX):
#   cmake -S examples/cmake -B build-cmake -DCMAKE_PREFIX_PATH=PREFIX
#   cmake --build build-cmake && ./build-cmake/greeter
cmake_minimum_required(VERSION 3.20)
project(ovis_cmake_example LANGUAGES C)

find_package(Ovis REQUIRED)

add_library(shout STATIC src/shout.c)
target_include_directories(shout PUBLIC include)

ovis_add_executable(greeter greeter.ovis LINK shout)
