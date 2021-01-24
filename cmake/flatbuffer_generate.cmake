# Copyright 2021 Kyle Ambroff-Kao <kyle@ambroffkao.com>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
if (_flatbuffer_generate_imported)
  return()
endif()

function(flatbuffer_generate)
  cmake_parse_arguments(flatbuffer_generate "WITH_GRPC" "OUTPUT;INCLUDE_PATH" "SCHEMAS" ${ARGN})

  get_target_property(FLATC_PATH flatbuffers::flatc IMPORTED_LOCATION_RELEASE)

  message("flatc location: ${FLATC_PATH}")

  file(WRITE "${PROJECT_BINARY_DIR}/flatcpath" "${FLATC_PATH}")

  file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/../clients/java/gradle.properties" "systemProp.flatcPath=${FLATC_PATH}")

  file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/generated)

  set(_flatbuffer_all_generated_files)

  foreach(_flatbuffer_file ${flatbuffer_generate_SCHEMAS})
    message("Generating code from ${_flatbuffer_file}")
    get_filename_component(_flatbuffer_filename ${_flatbuffer_file} NAME_WE)
    get_filename_component(_flatbuffer_abspath ${CMAKE_CURRENT_SOURCE_DIR}/${_flatbuffer_file} ABSOLUTE)
    set(_flatbuffer_generated_files
      ${CMAKE_CURRENT_BINARY_DIR}/generated/${_flatbuffer_filename}_generated.h
      )

    set(_flatc_extra_args)
    if (flatbuffer_generate_WITH_GRPC)
      set(_flatc_extra_args "--grpc")
      list(APPEND _flatbuffer_generated_files
        ${CMAKE_CURRENT_BINARY_DIR}/generated/${_flatbuffer_filename}.grpc.fb.h
        ${CMAKE_CURRENT_BINARY_DIR}/generated/${_flatbuffer_filename}.grpc.fb.cc
        )
    endif()

    message(STATUS "Compiling flatbuffer schema ${_flatbuffer_abspath}")
    execute_process(
      COMMAND ${FLATC_PATH} ${_flatc_extra_args} --cpp --reflect-names -I ${flatbuffer_generate_INCLUDE_PATH} -o ${CMAKE_CURRENT_BINARY_DIR}/generated ${_flatbuffer_abspath}
      RESULT_VARIABLE flatc_return_value
    )
    # Fail the build if we can't generate these files
    if (NOT (flatc_return_value EQUAL "0"))
      message( FATAL_ERROR "Bad exit status from flatc ${flatc_return_value}")
    endif()

    list(APPEND _flatbuffer_all_generated_files ${_flatbuffer_generated_files})
  endforeach()

  # Disable some warnings on these generated files since we don't have the ability to fix them easily.
  set_source_files_properties(${_flatbuffer_all_generated_files}
    PROPERTIES COMPILE_FLAGS "-Wno-error -Wno-class-memaccess -Wno-unknown-warning-option"
    )

  set(${flatbuffer_generate_OUTPUT} ${_flatbuffer_all_generated_files} PARENT_SCOPE)
endfunction()

function(generate_embedded_resource_header)
  cmake_parse_arguments(parsed "" "RESOURCE_PATH;FILE_RESOURCE" "" ${ARGN})
  message(STATUS "parameters \"${ARGN}\"\n resource file ${parsed_FILE_RESOURCE} path ${parsed_RESOURCE_PATH}")
  set(_source_file "${parsed_RESOURCE_PATH}/${parsed_FILE_RESOURCE}")
  string(MAKE_C_IDENTIFIER "${parsed_FILE_RESOURCE}" cpp_identifier)
  file(READ ${_source_file} resource_data)
  set(_generated_file "${CMAKE_CURRENT_BINARY_DIR}/generated/${parsed_FILE_RESOURCE}.hpp")
  # note, this is being generated in the global namespace
  file(WRITE "${_generated_file}" "\#pragma once\n\#include <string_view>\nconstexpr std::string_view  ${cpp_identifier} = R\"RAWSTRING(\n")
  file(APPEND "${_generated_file}" "${resource_data}")
  file(APPEND "${_generated_file}" ")RAWSTRING\";\n")
  message(STATUS "Generated resource file ${_generated_file}")
endfunction()

set(_flatbuffer_generate_imported TRUE)
