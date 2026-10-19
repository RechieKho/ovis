# find_package(Ovis): the `ovis` compiler, the runtime its programs link with
# (ovis::runtime) and ovis_add_executable. See docs/c-libraries.md.
#
#   find_package(Ovis REQUIRED)
#   add_library(mylib STATIC src/mylib.c)
#   target_include_directories(mylib PUBLIC include)
#   ovis_add_executable(app main.ovis LINK mylib)

get_filename_component(_ovis_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

find_program(OVIS_EXECUTABLE ovis HINTS "${_ovis_prefix}/bin" NO_DEFAULT_PATH)
if(NOT OVIS_EXECUTABLE)
  find_program(OVIS_EXECUTABLE ovis)
endif()

option(OVIS_FAST "Build Ovis programs without the run-time checks" OFF)

# The runtime is C source, built here with the project's C compiler and flags,
# so cross-compiling and sanitizers apply to it as to the rest.
set(OVIS_RUNTIME_SOURCE "${_ovis_prefix}/share/ovis/ovis_runtime.c")
if(NOT TARGET ovis_runtime AND EXISTS "${OVIS_RUNTIME_SOURCE}")
  add_library(ovis_runtime STATIC "${OVIS_RUNTIME_SOURCE}")
  set_target_properties(ovis_runtime PROPERTIES POSITION_INDEPENDENT_CODE ON
                                                C_STANDARD 11)
  target_link_libraries(ovis_runtime PUBLIC m)
  # Programs are checked unless OVIS_FAST is ON: the runtime and the object
  # ovis writes must agree (ovis_add_executable passes --fast then).
  if(NOT OVIS_FAST)
    target_compile_definitions(ovis_runtime PRIVATE OVIS_CHECKED)
  endif()
  add_library(ovis::runtime ALIAS ovis_runtime)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/OvisMacros.cmake")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Ovis REQUIRED_VARS OVIS_EXECUTABLE OVIS_RUNTIME_SOURCE)
