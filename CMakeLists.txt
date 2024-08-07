cmake_minimum_required(VERSION 3.10)
project(quicsend_library C CXX)

set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
    # -fsanitize=address
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -march=native")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_package(OpenSSL REQUIRED)

SET(Boost_USE_STATIC_LIBS ON)
SET(Boost_USE_MULTITHREAD ON)
find_package(Boost REQUIRED COMPONENTS system)

find_package(Python3 REQUIRED COMPONENTS Interpreter Development)

if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    set(QUICHE_LIB_PATH ${CMAKE_CURRENT_SOURCE_DIR}/quiche/target/release/libquiche.a)
    set(QUICHE_BUILD_CMD cargo build --features ffi --lib --release)
else()
    set(QUICHE_LIB_PATH ${CMAKE_CURRENT_SOURCE_DIR}/quiche/target/debug/libquiche.a)
    set(QUICHE_BUILD_CMD cargo build --features ffi --lib)
endif()

# Link to quiche
include(ExternalProject)
ExternalProject_Add(quiche
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/quiche
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ${QUICHE_BUILD_CMD}
    INSTALL_COMMAND ""
    BUILD_IN_SOURCE 1
)
add_custom_target(quiche_target DEPENDS quiche)
add_library(quiche_lib STATIC IMPORTED)
set_target_properties(quiche_lib PROPERTIES
    IMPORTED_LOCATION ${QUICHE_LIB_PATH}
)
target_include_directories(quiche_lib INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/quiche/quiche/include
)
add_dependencies(quiche_lib quiche_target)

file(GLOB_RECURSE DISTRIBUTED_HEADER_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp")
file(GLOB_RECURSE DISTRIBUTED_SRC_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

# Project library target
add_library(${PROJECT_NAME} SHARED
    ${DISTRIBUTED_HEADER_FILES}
    ${DISTRIBUTED_SRC_FILES}
)
target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${Python3_INCLUDE_DIRS}
)
target_link_libraries(${PROJECT_NAME} PUBLIC
    ${Python3_LIBRARIES}
    Boost::system
    quiche_lib
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "") # remove lib prefix
set_target_properties(${PROJECT_NAME} PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")

message(STATUS "CMAKE_LIBRARY_OUTPUT_DIRECTORY: ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
