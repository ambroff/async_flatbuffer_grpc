# Copyright 2018 The Cartographer Authors
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

cmake_minimum_required(VERSION 3.11)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(HunterGate)
HunterGate(
  URL "https://github.com/ambroff/hunter/archive/incubator-2020-08-27_4.zip"
  SHA1 "eea132eafa4b99b9737d5aa8f96e9bec01280d23"
  LOCAL
)

project(async_grpc)

set(ASYNC_GRPC_MAJOR_VERSION 0)
set(ASYNC_GRPC_MINOR_VERSION 1)
set(ASYNC_GRPC_PATCH_VERSION 0)
set(ASYNC_GRPC_VERSION ${ASYNC_GRPC_MAJOR_VERSION}.${ASYNC_GRPC_MINOR_VERSION}.${ASYNC_GRPC_PATCH_VERSION})
set(ASYNC_GRPC_SOVERSION ${ASYNC_GRPC_MAJOR_VERSION}.${ASYNC_GRPC_MINOR_VERSION})

include("${PROJECT_SOURCE_DIR}/cmake/functions.cmake")
google_initialize_async_grpc_project()
enable_testing()

hunter_add_package(GTest)
find_package(GTest CONFIG REQUIRED)

hunter_add_package(flatbuffers)
find_package(Flatbuffers CONFIG REQUIRED)
include(flatbuffer_generate)

hunter_add_package(Protobuf)
find_package(Protobuf CONFIG REQUIRED)

hunter_add_package(glog)
find_package(glog CONFIG REQUIRED)

hunter_add_package(gflags)
find_package(gflags CONFIG REQUIRED)

hunter_add_package(gRPC)
find_package(gRPC CONFIG REQUIRED)

set(ALL_LIBRARY_HDRS
    async_grpc/async_client.h
    async_grpc/client.h
    async_grpc/common/blocking_queue.h
    async_grpc/common/make_unique.h
    async_grpc/common/mutex.h
    async_grpc/common/optional.h
    async_grpc/common/port.h
    async_grpc/common/time.h
    async_grpc/completion_queue_pool.h
    async_grpc/completion_queue_thread.h
    async_grpc/event_queue_thread.h
    async_grpc/execution_context.h
    async_grpc/retry.h
    async_grpc/rpc.h
    async_grpc/rpc_handler_interface.h
    async_grpc/rpc_handler.h
    async_grpc/rpc_service_method_traits.h
    async_grpc/server.h
    async_grpc/service.h
    async_grpc/span.h
    async_grpc/testing/rpc_handler_test_server.h
    async_grpc/testing/rpc_handler_wrapper.h
    async_grpc/type_traits.h)

set(ALL_LIBRARY_SRCS
    async_grpc/common/time.cc
    async_grpc/completion_queue_pool.cc
    async_grpc/completion_queue_thread.cc
    async_grpc/event_queue_thread.cc
    async_grpc/retry.cc
    async_grpc/rpc.cc
    async_grpc/server.cc
    async_grpc/service.cc)

set(ALL_TESTS
    async_grpc/client_test.cc
    async_grpc/server_test.cc
    async_grpc/type_traits_test.cc)

set(ALL_PROTOS
    async_grpc/proto/math_service.proto)

flatbuffer_generate(
  WITH_GRPC
  OUTPUT ALL_TEST_GRPC_FILES
  INCLUDE_PATH async_grpc/schemas
  SCHEMAS async_grpc/schemas/math_service.fbs
  )

set_source_files_properties(${ALL_TEST_GRPC_FILES} PROPERTIES GENERATED TRUE)

add_library(${PROJECT_NAME} ${ALL_LIBRARY_HDRS} ${ALL_LIBRARY_SRCS} ${ALL_TEST_GRPC_FILES})

foreach(RELATIVEPATH ${ALL_TESTS})
  get_filename_component(DIR ${RELATIVEPATH} DIRECTORY)
  get_filename_component(FILENAME ${RELATIVEPATH} NAME_WE)
  # Replace slashes as required for CMP0037.
  string(REPLACE "/" "." TEST_TARGET_NAME "${DIR}/${FILENAME}")
  google_test("${TEST_TARGET_NAME}" ${RELATIVEPATH})
  target_link_libraries(${TEST_TARGET_NAME} PUBLIC GTest::gmock_main)
  target_link_libraries("${TEST_TARGET_NAME}" PUBLIC gRPC::grpc++ flatbuffers::flatbuffers)
  target_link_libraries("${TEST_TARGET_NAME}" PUBLIC flatbuffers::flatbuffers)
  target_include_directories("${TEST_TARGET_NAME}" PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
endforeach()

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

target_link_libraries(${PROJECT_NAME} PUBLIC glog::glog)
target_link_libraries(${PROJECT_NAME} PUBLIC gflags)

target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC
  ${PROTOBUF_INCLUDE_DIR})
# TODO(hrapp): This should not explicitly list pthread and use
# PROTOBUF_LIBRARIES, but that failed on first try.
target_link_libraries(${PROJECT_NAME} PUBLIC protobuf::libprotobuf pthread)

# TODO(cschuet): Write FindGRPC.cmake
target_link_libraries(${PROJECT_NAME} PUBLIC gRPC::grpc++)

target_link_libraries(${PROJECT_NAME} PUBLIC flatbuffers::flatbuffers)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE
  "${GMOCK_INCLUDE_DIRS}")

set(TARGET_COMPILE_FLAGS "${TARGET_COMPILE_FLAGS} ${GOOG_CXX_FLAGS}")
set_target_properties(${PROJECT_NAME} PROPERTIES
  COMPILE_FLAGS ${TARGET_COMPILE_FLAGS})

install(
  TARGETS ${PROJECT_NAME}
  EXPORT AsyncGrpcExport
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

foreach(HDR ${ALL_LIBRARY_HDRS})
  get_filename_component(DIR ${HDR} DIRECTORY)
  install(
    FILES ${HDR}
    DESTINATION include/${DIR}
  )
endforeach()

set(ASYNC_GRPC_CMAKE_DIR share/async_grpc/cmake)
include(CMakePackageConfigHelpers)
configure_package_config_file(
  async_grpc-config.cmake.in
  ${PROJECT_BINARY_DIR}/cmake/async_grpc/async_grpc-config.cmake
  PATH_VARS ASYNC_GRPC_CMAKE_DIR
  INSTALL_DESTINATION ${CMAKE_INSTALL_PREFIX}/share/async_grpc
)

install(
  EXPORT AsyncGrpcExport
  DESTINATION share/async_grpc/cmake/
  FILE AsyncGrpcTargets.cmake
)

install(
  FILES ${PROJECT_BINARY_DIR}/cmake/async_grpc/async_grpc-config.cmake
  DESTINATION share/async_grpc/
)
