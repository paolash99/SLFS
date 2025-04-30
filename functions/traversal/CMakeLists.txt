
cmake_minimum_required(VERSION 3.5.1)

set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER "g++")

project(exec)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if (BUILD_AS_ACTIONLOOP)
    set(CMAKE_CXX_FLAGS "-DAS_ACTIONLOOP")
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -DBOOST_THREAD_PROVIDES_EXECUTORS -DBOOST_THREAD_USES_MOVE -DZKPP_FUTURE_USE_BOOST")
set(CMAKE_CXX_FLAGS_DEBUG "-ggdb -Wextra -Werror -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_COMPILE_WARNING_AS_ERROR ON)
set(CMAKE_EXE_LINKER_FLAGS "-static -static-libgcc -static-libstdc++")

set(CMAKE_CXX_STANDARD 20)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)

set(Boost_INSTALL_DIR ${CONAN_BOOST_ROOT})

conan_basic_setup()

if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
    set(CROSS_LINKER_FLAGS "")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
endif()

include_directories(/action/libslsfs)
include_directories(/action/libslsfs/slsfs)
include_directories(/minio-cpp-build/vcpkg_installed/x64-linux/include)

link_directories(/minio-cpp-build/vcpkg_installed/x64-linux/lib)

add_executable(exec entry.cpp)
add_executable(datachannel-copy-paste-offerer offerer.cpp)
add_executable(datachannel-copy-paste-answerer answerer.cpp)

set(CMAKE_PCH_INSTANTIATE_TEMPLATES ON)
target_precompile_headers(exec PRIVATE <boost/asio.hpp>)
target_precompile_headers(exec PRIVATE <boost/signals2.hpp>)
target_precompile_headers(exec PRIVATE <boost/iostreams/stream.hpp>)
target_precompile_headers(exec PRIVATE <oneapi/tbb/concurrent_hash_map.h>)
target_precompile_headers(exec PRIVATE <fmt/core.h>)
target_precompile_headers(exec PRIVATE <nlohmann/json.hpp>)
target_precompile_headers(exec PRIVATE <miniocpp/client.h>)
target_precompile_headers(exec PRIVATE <Poco/Crypto/DigestEngine.h>)

set(CUSTOM_LIBRARIES "-ldatachannel-static -ljuice-static -lusrsctp -lssl -lcrypto -ltbb -ltbbmalloc -lminiocpp -lcurlpp -lpugixml -linih")

target_link_libraries(exec         ${CUSTOM_LIBRARIES} ${CROSS_LINKER_FLAGS} ${CONAN_LIBS})
target_link_libraries(datachannel-copy-paste-offerer ${CUSTOM_LIBRARIES} ${CROSS_LINKER_FLAGS} ${CONAN_LIBS})
target_link_libraries(datachannel-copy-paste-answerer ${CUSTOM_LIBRARIES} ${CROSS_LINKER_FLAGS} ${CONAN_LIBS})
