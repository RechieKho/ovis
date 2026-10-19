# ovis_add_executable(NAME PROGRAM [LINK TARGET...])
#
# Builds an Ovis program (a .ovis file, or a folder with a main.ovis) into an
# executable. Ovis writes an object file and CMake links it with the runtime
# and the LINK targets, so static or shared, dependencies and rpath are the
# targets' own. Headers named by `use "x.h"` are read with the include
# directories and definitions of the LINK targets.
#
# The build is locked (Ovis never asks the model or edits the source tree):
# run `ovis run PROGRAM` once to settle unclear lines and commit the result.
# Set OVIS_UNLOCKED to ON to let the build do what `ovis run` does.
option(OVIS_UNLOCKED "Let ovis_add_executable build programs with unsettled lines" OFF)
option(OVIS_FAST "Build Ovis programs without the run-time checks" OFF)

function(ovis_add_executable name program)
  cmake_parse_arguments(PARSE_ARGV 2 ARG "" "" "LINK")
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
  add_custom_command(
    OUTPUT "${object}"
    COMMAND "${OVIS_EXECUTABLE}" build "${source}" --object "${object}"
            --link-by-caller ${locked} --quiet --depfile "${depfile}" "@${flags}"
    DEPENDS ${inputs} "${flags}"
    DEPFILE "${depfile}"
    COMMENT "Compiling ${name} with Ovis"
    VERBATIM)
endfunction()
