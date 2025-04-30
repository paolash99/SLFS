FROM gcc:12 AS builder

RUN apt-get update && \
    apt-get install -y --no-install-recommends python3-pip make cmake ninja-build ccache git wget libzookeeper-mt-dev clang openssh-server && \
    pip3 install conan==1.62 --break-system-packages

RUN apt-get update && \
    apt-get install -y --no-install-recommends default-jdk && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Set JAVA_HOME environment variable
ENV JAVA_HOME /usr/lib/jvm/java-11-openjdk-amd64
ENV PATH $JAVA_HOME/bin:$PATH

RUN conan profile new default --detect && \
    conan profile update settings.compiler.libcxx=libstdc++11 default

ADD profiles /pre/profiles

ADD conanfile.txt /pre
ENV CONAN_REVISIONS_ENABLED=1

RUN --mount=type=cache,target=/ccache \
    mkdir /pre/build && cd /pre/build && \
    conan install .. --profile ../profiles/release-native --build missing && \
    conan install .. --profile ../profiles/debug --build missing

ADD zookeeper-cpp /zookeeper-cpp

RUN mkdir /zookeeper-cpp/build && cd /zookeeper-cpp/build && \
    conan install .. --profile /pre/profiles/release-native --build missing && \
    cmake .. -G Ninja \
             -DCMAKE_BUILD_TYPE=Release \
             -DCMAKE_CXX_FLAGS='-std=c++17 -include "utility" -DZKPP_FUTURE_USE_BOOST=1 -fPIC' && \
    cmake --build . && ninja install

ADD . /final

ARG debug

RUN --mount=type=cache,target=/final/build \
    cd /final && \
    bash -c 'if [[ -z "$debug" ]]; then make release; else make debug; fi' && \
    cp /final/build/bin/* /bin && \
    cp /final/build/lib/* /lib && \
    chmod +x /bin/slsfs-proxy

# Stage 2: Final Image
FROM ubuntu:23.04

RUN apt-get update && \
    apt-get install -y --no-install-recommends gdb iproute2

# Install SSH server
RUN apt-get install -y --no-install-recommends openssh-server

RUN apt-get update && \
    apt install -y openjdk-8-jdk && \
    rm -rf /var/lib/apt/lists/*

# Set JAVA_HOME environment variable
ENV JAVA_HOME /usr/lib/jvm/java-8-openjdk-amd64
ENV PATH $JAVA_HOME/bin:$PATH

ADD backend /backend

COPY start-proxy-script.sh /bin/start-proxy-script.sh
COPY HopsFSBenchmark-1.0-jar-with-dependencies.jar /bin/slsfs-benchmark.jar
COPY benchmark-config.yaml /bin/benchmark-config.yaml
COPY benchmark-workload.yaml /bin/benchmark-workload.yaml
COPY slsfs.pem /home/ubuntu/.ssh/id_rsa
RUN chmod 600 /home/ubuntu/.ssh/id_rsa
RUN chown ubuntu:ubuntu /home/ubuntu/.ssh/id_rsa

COPY createSubtree.txt /createSubtree.txt
COPY createSubtree2.txt /createSubtree2.txt
COPY createSubtree3.txt /createSubtree3.txt
COPY createSubtree4.txt /createSubtree4.txt
COPY createSubtree5.txt /createSubtree5.txt
# Mount SSH key from host
COPY slsfs.pem /root/.ssh/id_rsa
RUN chmod 600 /root/.ssh/id_rsa

# Disable strict host key checking
RUN echo "StrictHostKeyChecking no" >> /etc/ssh/ssh_config

COPY --from=builder /usr/lib/x86_64-linux-gnu/libzookeeper_mt.so.2 /lib/libzookeeper_mt.so.2
COPY --from=builder /lib/x86_64-linux-gnu/librt.so.1 /lib/librt.so.1
COPY --from=builder /lib/x86_64-linux-gnu/libdl.so.2 /lib/libdl.so.2

COPY --from=builder /bin/slsfs-proxy /bin/slsfs-proxy
COPY --from=builder /bin/slsfs-cmd /bin/slsfs-cmd
COPY --from=builder /lib/libslsfs-client-benchmark.so /usr/lib/libslsfs-client-benchmark.so

# Expose Docker socket to the container
VOLUME /var/run/docker.sock

# Start SSH service
RUN service ssh start

ENTRYPOINT ["/bin/slsfs-proxy"]
