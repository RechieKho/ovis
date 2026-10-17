cmake_minimum_required(VERSION 3.20.0)

Include(FetchContent)

project(
    ovis
    VERSION 0.1.0
    LANGUAGES C CXX
)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.8.1
)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
  GIT_SHALLOW    TRUE
)
FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG        v0.18.3
  GIT_SHALLOW    TRUE
)
option(OVIS_BUILTIN_LLM "Run the translation model in-process with llama.cpp" ON)
set(OVIS_EMBED_MODEL "" CACHE FILEPATH
    "GGUF model to embed in the ovis executable (optional)")
if(OVIS_BUILTIN_LLM)
  FetchContent_Declare(
    llama
    GIT_REPOSITORY https://github.com/ggml-org/llama.cpp.git
    GIT_TAG        b11200
    GIT_SHALLOW    TRUE
  )
  set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
  set(LLAMA_BUILD_COMMON OFF CACHE INTERNAL "")
  set(LLAMA_BUILD_TESTS OFF CACHE INTERNAL "")
  set(LLAMA_BUILD_TOOLS OFF CACHE INTERNAL "")
  set(LLAMA_BUILD_EXAMPLES OFF CACHE INTERNAL "")
  set(LLAMA_BUILD_SERVER OFF CACHE INTERNAL "")
  FetchContent_MakeAvailable(llama)
endif()

set(JSON_BuildTests OFF CACHE INTERNAL "")
set(HTTPLIB_COMPILE OFF CACHE INTERNAL "")
set(HTTPLIB_REQUIRE_OPENSSL OFF CACHE INTERNAL "")
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE INTERNAL "")
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(Catch2 nlohmann_json httplib)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})
if(LLVM_LINK_LLVM_DYLIB)
  set(LLVM_LIBS LLVM)
else()
  llvm_map_components_to_libnames(LLVM_LIBS core support passes target native)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test)
set(INC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
file(GLOB LIBRARY_SRC_FILES ${SRC_DIR}/ovis/*.cpp)
file(GLOB TEST_FILES ${TEST_DIR}/*.cpp)

set(LIBRARY_NAME lib${CMAKE_PROJECT_NAME})
set(EXECUTABLE_NAME ${CMAKE_PROJECT_NAME})
set(TEST_NAME test_${CMAKE_PROJECT_NAME})

add_library(${LIBRARY_NAME} STATIC ${LIBRARY_SRC_FILES})
target_include_directories(${LIBRARY_NAME} PUBLIC ${INC_DIR})
target_include_directories(${LIBRARY_NAME} SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS})
target_link_libraries(
    ${LIBRARY_NAME}
    PUBLIC ${LLVM_LIBS} nlohmann_json::nlohmann_json httplib::httplib
)

if(OVIS_BUILTIN_LLM)
  target_link_libraries(${LIBRARY_NAME} PUBLIC llama)
  target_compile_definitions(${LIBRARY_NAME} PUBLIC OVIS_HAS_BUILTIN_LLM)
endif()

add_executable(${EXECUTABLE_NAME} ${SRC_DIR}/main.cpp)
target_link_libraries(${EXECUTABLE_NAME} PRIVATE ${LIBRARY_NAME})
if(OVIS_EMBED_MODEL)
  add_custom_command(
    TARGET ${EXECUTABLE_NAME} POST_BUILD
    COMMAND $<TARGET_FILE:${EXECUTABLE_NAME}> model embed ${OVIS_EMBED_MODEL}
            -o $<TARGET_FILE:${EXECUTABLE_NAME}>
    COMMENT "Embedding ${OVIS_EMBED_MODEL} in ovis"
    VERBATIM
  )
endif()

add_executable(${TEST_NAME} ${TEST_FILES})
target_link_libraries(${TEST_NAME} PRIVATE ${LIBRARY_NAME} Catch2::Catch2WithMain)
target_compile_definitions(
    ${TEST_NAME} PRIVATE OVIS_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/examples"
)
catch_discover_tests(${TEST_NAME})
