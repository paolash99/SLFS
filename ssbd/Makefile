RELEASE_FLAGS=-static -s
VERBOSE=-DCMAKE_VERBOSE_MAKEFILE=ON

CC ?= cc
CXX ?= c++

.PHONY: release debug from-docker setup

from-docker:
	DOCKER_BUILDKIT=1 docker build -t hare1039/ssbd:0.0.1 .
	docker run --rm --entrypoint cat hare1039/ssbd:0.0.1 /final/build/bin/run > run
	chmod +x run
	docker run --rm --entrypoint cat hare1039/ssbd:0.0.1 /final/build/bin/client > client
	chmod +x client

debug-from-docker:
	DOCKER_BUILDKIT=1 docker build -t hare1039/ssbd:0.0.1 . --build-arg debug=true
	docker run --rm --entrypoint cat hare1039/ssbd:0.0.1 /final/build/bin/client > client
	chmod +x client
	docker run --rm --entrypoint cat hare1039/ssbd:0.0.1 /final/build/bin/run > run
	chmod +x run

release-all: release debug
	echo "build all"

release:
	mkdir -p build && \
	cd build && \
	conan install .. --profile ../profiles/release-native --build missing && \
	cmake .. -G Ninja \
	         -DCMAKE_BINARY_DIR=/ccache \
             -DCMAKE_BUILD_TYPE=Release \
             -DCMAKE_C_COMPILER=${CC}   \
             -DCMAKE_CXX_COMPILER=${CXX} && \
	CCACHE_SLOPPINESS=pch_defines,time_macros cmake --build .

debug:
	mkdir -p build && \
    cd build && \
    conan install .. --profile ../profiles/debug --build missing && \
    cmake .. -G Ninja \
	         -DCMAKE_BINARY_DIR=/ccache \
             -DCMAKE_BUILD_TYPE=Debug \
             -DCMAKE_C_COMPILER=${CC}   \
             -DCMAKE_CXX_COMPILER=${CXX} && \
	CCACHE_SLOPPINESS=pch_defines,time_macros cmake --build .

clean:
	rm -rf build-*
