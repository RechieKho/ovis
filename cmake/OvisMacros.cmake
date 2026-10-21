# ovis_add_executable(NAME PROGRAM [LINK TARGET...] [USE_LINKS])
#
# Builds an Ovis program (a .ovis file, or a folder with a main.ovis) into an
# executable. Ovis writes an object file and CMake links it with the runtime
# and the LINK targets, so static or shared, dependencies and rpath are the
# targets' own. Headers named by `use "x.h"` are read with the include
# directories and definitions of the LINK targets.
#
# USE_LINKS also links the libraries the program's own `use` lines name
# (pkg-config's or -l), as `ovis build` would: the projects ovis generates
# from ovis.toml use it.
#
# The build is locked (Ovis never asks the model or edits the source tree):
# run `ovis run PROGRAM` once to settle unclear lines and commit the result.
# Set OVIS_UNLOCKED to ON to let the build do what `ovis run` does.
# OVIS_FLAGS: more options for ovis (a list, e.g. --offline;--phrases;DIR).
option(OVIS_UNLOCKED "Let ovis_add_executable build programs with unsettled lines" OFF)
option(OVIS_FAST "Build Ovis programs without the run-time checks" OFF)
set(OVIS_FLAGS "" CACHE STRING "More options for ovis, as a list")

function(ovis_add_executable name program)
  cmake_parse_arguments(PARSE_ARGV 2 ARG "USE_LINKS" "" "LINK")
  if(NOT OVIS_EXECUTABLE)
    message(FATAL_ERROR "ovis_add_executable: the ovis compiler was not found")
  endif()
  get_filename_component(source "${program}" ABSOLUTE)
  if(IS_DIRECTORY "${source}")
    file(GLOB_RECURSE inputs CONFIGURE_DEPENDS "${source}/*.ovis" "${source}/*.ovis.lock")
  else()
    set(inputs "${source}")
    if(EXISTS "${source}.lock")
      list(APPEND inputs "${source}.lock")
    endif()
  endif()

  set(object "${CMAKE_CURRENT_BINARY_DIR}/${name}.ovis.o")
  set(flags "${CMAKE_CURRENT_BINARY_DIR}/${name}.ovis.rsp")
  set(depfile "${CMAKE_CURRENT_BINARY_DIR}/${name}.ovis.d")

  # The executable exists before its flags are written, so they can name the
  # include directories and definitions it gets from the LINK targets.
  set_source_files_properties("${object}" PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
  add_executable(${name} "${object}")
  set_target_properties(${name} PROPERTIES LINKER_LANGUAGE C)
  target_link_libraries(${name} PRIVATE ${ARG_LINK} ovis::runtime)

  set(includes "$<TARGET_PROPERTY:${name},INCLUDE_DIRECTORIES>")
  set(definitions "$<TARGET_PROPERTY:${name},COMPILE_DEFINITIONS>")
  file(GENERATE OUTPUT "${flags}"
       CONTENT "$<$<BOOL:${includes}>:-I\"$<JOIN:${includes},\"\n-I\">\"\n>$<$<BOOL:${definitions}>:-D\"$<JOIN:${definitions},\"\n-D\">\"\n>")

  set(locked --locked)
  if(OVIS_UNLOCKED)
    set(locked)
  endif()
  if(OVIS_FAST)
    list(APPEND locked --fast)
  endif()
  # The libraries come from ovis, as a response file the linker reads.
  set(outputs "${object}")
  set(linking --link-by-caller)
  if(ARG_USE_LINKS)
    set(links "${CMAKE_CURRENT_BINARY_DIR}/${name}.ovis.links")
    list(APPEND outputs "${links}")
    set(linking --link-flags "${links}")
    target_link_options(${name} PRIVATE "@${links}")
  endif()
  add_custom_command(
    OUTPUT ${outputs}
    COMMAND "${OVIS_EXECUTABLE}" build "${source}" --object "${object}"
            ${linking} ${locked} --quiet --depfile "${depfile}" "@${flags}"
            ${OVIS_FLAGS}
    DEPENDS ${inputs} "${flags}"
    DEPFILE "${depfile}"
    COMMENT "Compiling ${name} with Ovis"
    VERBATIM)
endfunction()
