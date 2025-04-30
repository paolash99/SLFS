cmake_minimum_required(VERSION 3.5.1)
#avoid cmake adds -Wl,-search_paths_first that makes mac compiled binary segmentation fault
set(HAVE_FLAG_SEARCH_PATHS_FIRST 0)

project(run)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "-Wall")
set(CMAKE_CXX_FLAGS_DEBUG "-g -Wextra -Werror")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

set(CMAKE_CXX_STANDARD 20)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)

set(Boost_INSTALL_DIR ${CONAN_BOOST_ROOT})

conan_basic_setup()

#set(CROSS_LINKER_FLAGS "-static -static-libgcc -static-libstdc++")
set(CROSS_LINKER_FLAGS "-static -lzkpp -lzookeeper_mt")




if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
    set(CROSS_LINKER_FLAGS "")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
endif()

add_executable(trace_emulator trace_emulator.cpp)

set(CMAKE_PCH_INSTANTIATE_TEMPLATES ON)
target_precompile_headers(trace_emulator PRIVATE <boost/asio.hpp>)
target_precompile_headers(trace_emulator PRIVATE <fmt/core.h>)
target_precompile_headers(trace_emulator PRIVATE <boost/filesystem.hpp>)
target_precompile_headers(trace_emulator PRIVATE <nlohmann/json.hpp>)

target_link_libraries(trace_emulator ${CONAN_LIBS} ${CROSS_LINKER_FLAGS})

IF ("${CMAKE_SYSTEM_NAME}" MATCHES "Windows")
   target_link_libraries(run ws2_32 wsock32)
ENDIF ()
