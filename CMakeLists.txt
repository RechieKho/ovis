cmake_minimum_required(VERSION 3.20.0)

Include(FetchContent)

project(
    ovis
    VERSION 0.0.1
    LANGUAGES C CXX
)

FetchContent_Declare(
  fast_float
  GIT_REPOSITORY https://github.com/fastfloat/fast_float.git
  GIT_TAG tags/v8.0.2
  GIT_SHALLOW TRUE
)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.8.1 
)
option(FASTFLOAT_INSTALL OFF)
FetchContent_MakeAvailable(Catch2 fast_float)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})
llvm_map_components_to_libnames(LLVM_LIBS core support)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test)
set(INC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
file(GLOB SRC_FILES ${SRC_DIR}/*.cpp)
file(GLOB TEST_FILES ${TEST_DIR}/*.cpp)

set(LIBRARY_NAME lib${CMAKE_PROJECT_NAME})
set(EXECUTABLE_NAME ${CMAKE_PROJECT_NAME})
set(TEST_NAME test_${CMAKE_PROJECT_NAME})

add_library(${LIBRARY_NAME} INTERFACE)
target_include_directories(${LIBRARY_NAME} INTERFACE ${INC_DIR} ${LLVM_INCLUDE_DIRS})
target_link_libraries(${LIBRARY_NAME} INTERFACE ${LLVM_LIBS} fast_float)

add_executable(
    ${EXECUTABLE_NAME}
    ${SRC_FILES}
)
target_link_libraries(${EXECUTABLE_NAME} PRIVATE ${LIBRARY_NAME})

add_executable(
    ${TEST_NAME}
    ${TEST_FILES}
)
target_link_libraries(${TEST_NAME} PRIVATE ${LIBRARY_NAME} Catch2::Catch2WithMain)
catch_discover_tests(${TEST_NAME})
