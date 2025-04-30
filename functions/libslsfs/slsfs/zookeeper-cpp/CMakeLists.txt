cmake_minimum_required(VERSION 3.5)

file(READ src/zk/config.hpp CONFIG_HPP_STR)
string(REGEX REPLACE ".*# *define +ZKPP_VERSION_MAJOR +([0-9]+).*" "\\1" ZKPP_VERSION_MAJOR "${CONFIG_HPP_STR}")
string(REGEX REPLACE ".*# *define +ZKPP_VERSION_MINOR +([0-9]+).*" "\\1" ZKPP_VERSION_MINOR "${CONFIG_HPP_STR}")
string(REGEX REPLACE ".*# *define +ZKPP_VERSION_PATCH +([0-9]+).*" "\\1" ZKPP_VERSION_PATCH "${CONFIG_HPP_STR}")

set(ZKPP_VERSION "${ZKPP_VERSION_MAJOR}.${ZKPP_VERSION_MINOR}.${ZKPP_VERSION_PATCH}")
project(zookeeper-cpp
        LANGUAGES CXX
        VERSION "${ZKPP_VERSION}"
       )

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)

set(Boost_INSTALL_DIR ${CONAN_BOOST_ROOT})

conan_basic_setup()

set(PROJECT_SO_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
message(STATUS "Software Version: ${ZKPP_VERSION}")

################################################################################
# CMake                                                                        #
################################################################################

cmake_policy(VERSION 3.5)
cmake_policy(SET CMP0037 OLD) # allow generation of "test" target
set(CMAKE_REQUIRED_QUIET YES) # tell check_include_file_cxx to keep quiet

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules/")

include(BuildFunctions)
include(CheckIncludeFileCXX)
include(ConfigurationSetting)
include(ListSplit)
include(ZooKeeper)

################################################################################
# Build Configuration                                                          #
################################################################################

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug")
  message(STATUS "No build type selected, default to ${CMAKE_BUILD_TYPE}")
endif()

set(VALID_BUILD_TYPES Debug Release)
if(NOT ${CMAKE_BUILD_TYPE} IN_LIST VALID_BUILD_TYPES)
  message(FATAL_ERROR "Invalid CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}\nValid build types are: ${VALID_BUILD_TYPES}")
endif()
message(STATUS "Configuration: ${CMAKE_BUILD_TYPE}")

message(STATUS "Features:")
build_option(NAME       CODE_COVERAGE
             DOC        "Enable code coverage (turns on the test-coverage target)"
             DEFAULT    OFF
             CONFIGS_ON Debug
            )

configuration_setting(NAME    BUFFER
                      DOC     "Type to use for zk::buffer"
                      DEFAULT STD_VECTOR
                      OPTIONS
                        STD_VECTOR
                        STD_STRING
                        CUSTOM
                     )

configuration_setting(NAME    FUTURE
                      DOC     "Type to use for zk::future<T> and zk::promise<T>"
                      DEFAULT STD
                      OPTIONS
                        STD
                        STD_EXPERIMENTAL
                        BOOST
                        CUSTOM
                     )

set(CXX_STANDARD c++17
    CACHE STRING "The language standard to target for C++."
   )

set(CXX_WARNINGS "-Wall -Wextra -Wconversion")
set(CXX_EXTRA_FLAGS "-Wl,--no-as-needed")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=${CXX_STANDARD} ${CXX_WARNINGS} -ggdb3 ${CXX_EXTRA_FLAGS}")
set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_CXX_FLAGS_DEBUG} -DZKPP_DEBUG=1")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

################################################################################
# External Libraries                                                           #
################################################################################

find_library(zookeeper_LIBRARIES zookeeper_mt)
set(ZKPP_LIB_DEPENDENCIES ${ZKPP_LIB_DEPENDENCIES} ${zookeeper_LIBRARIES})

include_directories("${PROJECT_SOURCE_DIR}/src")

if (ZKPP_BUILD_SETTING_FUTURE STREQUAL "BOOST")
    find_package(Boost
                 1.52.0
                 REQUIRED
                 thread)
    set(ZKPP_LIB_DEPENDENCIES ${ZKPP_LIB_DEPENDENCIES} ${Boost_LIBRARIES})
endif()


################################################################################
# Building                                                                     #
################################################################################

build_module(NAME zkpp
             PATH src/zk
             NO_RECURSE
             LINK_LIBRARIES
             ${ZKPP_LIB_DEPENDENCIES} ${CONAN_LIBS}
             )

install(TARGETS zkpp
        LIBRARY
          DESTINATION lib)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        FILES_MATCHING
          DESTINATION include
          FILES_MATCHING
            PATTERN "*.h*")
