FROM alpine:3.17 AS builder

RUN apk update && \
    apk add --no-cache \
        py3-pip cmake make ccache git wget bash g++ gcc linux-headers perl pkgconf clang llvm \
        zlib-dev openssl-dev zstd-dev build-base ninja zip unzip curl-dev curl inih-dev jq gmp-dev && \
    pip3 install "conan==1.59"

RUN git clone --branch 2022.11.14 https://github.com/Microsoft/vcpkg.git /vcpkg && \
    bash -c 'export VCPKG_FORCE_SYSTEM_BINARIES=1; /vcpkg/bootstrap-vcpkg.sh -disableMetrics'

RUN git clone https://github.com/minio/minio-cpp /minio-cpp && \
    mkdir /minio-cpp-build && \
    cat /minio-cpp/vcpkg.json |\
        jq '.overrides = [{ name: "openssl", "version-string": "1.1.1n" }]' |\
        jq '."builtin-baseline" = "09adfdc8cdad76345b7cc7f3305899e1cbd66297"' > m.json && \
    mv m.json /minio-cpp/vcpkg.json && \
    bash -c 'export VCPKG_FORCE_SYSTEM_BINARIES=1; cmake -G Ninja -B /minio-cpp-build -S /minio-cpp -DCMAKE_TOOLCHAIN_FILE=/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_C_COMPILER=cc -DCMAKE_CXX_COMPILER=c++' && \
    bash -c 'export VCPKG_FORCE_SYSTEM_BINARIES=1; cd /minio-cpp-build && ninja && ninja install'

RUN git clone --branch v2021.7.0 https://github.com/oneapi-src/oneTBB.git && \
    mkdir -p /oneTBB/build && cd /oneTBB/build && \
    cmake -G Ninja -DTBB_TEST=OFF -DBUILD_SHARED_LIBS=OFF .. && \
    ninja && ninja install

RUN git clone --branch OpenSSL_1_1_1t git://git.openssl.org/openssl.git /openssl && \
    cd /openssl && \
    ./config -static && \
    make -j && make install

RUN git clone --branch v0.18.3 https://github.com/paullouisageneau/libdatachannel.git /libdatachannel && \
    cd /libdatachannel && \
    git submodule update --init --recursive --depth 1 && \
    cmake -B build \
        -DUSE_GNUTLS=0 \
        -DUSE_NICE=0 \
        -DOPENSSL_ROOT_DIR=/openssl \
        -DCMAKE_BUILD_TYPE=Release \
        -DNO_MEDIA=1 && \
    cd build && \
    make -j datachannel-static && make install

RUN find /libdatachannel -name '*.a' -exec cp {} /lib \;

RUN conan profile new default --detect &&\
    conan profile update settings.compiler.libcxx=libstdc++11 default

ADD traversal/profiles      /pre/profiles
ADD traversal/conanfile.txt /pre

RUN --mount=type=cache,target=/action/build mkdir -p /action/build && \
    conan install /pre --profile /pre/profiles/release --build=missing --build=b2 --build=boost --build=librdkafka --build=cassandra-cpp-driver --build=poco --build=openssl --build=m4 --build=libcurl --build=libuv && \
    conan install /pre --profile /pre/profiles/debug   --build=missing --build=b2 --build=boost --build=librdkafka --build=cassandra-cpp-driver --build=poco --build=openssl --build=m4 --build=libcurl --build=libuv

ADD libslsfs /action/libslsfs
ADD traversal /action/

ARG debug
ARG actionloop

RUN --mount=type=cache,target=/action/build mkdir -p /action/build && \
    cd /action/build && \
    bash -c 'if [[ -z "$debug" ]]; then conan install .. --profile /pre/profiles/release; else conan install .. --profile /pre/profiles/debug; fi' && \
    bash -c 'if [[ -z "$debug" ]]; then cmake -GNinja .. -DCMAKE_BUILD_TYPE=Release -DBUILD_AS_ACTIONLOOP=$actionloop; else cmake .. -GNinja -DCMAKE_BUILD_TYPE=Debug -DBUILD_AS_ACTIONLOOP=$actionloop; fi' && \
    cmake --build . && \
    cp /action/build/bin/* /action/
