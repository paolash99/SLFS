FROM hare1039/dockerskeleton:latest AS builder

RUN apk update && \
    apk add --no-cache --virtual .build-deps \
                                 bzip2-dev \
                                 g++ \
                                 libc-dev \
                                 cassandra-cpp-driver-dev \
                                 openssl-libs-static zlib-static cyrus-sasl-static \
                                 curl-dev curl-static nghttp2-static zstd-static libuv-static brotli-static \
                                 boost-dev boost-static ccache \
                                 make

ADD appv2 /action/

ARG CPPBUILDARGS
ENV CCACHE_DIR=/ccache

RUN cd /action && time g++ -std=c++20 -o exec -static *.cpp $CPPBUILDARGS     -lboost_coroutine -lboost_chrono -lboost_iostreams -lboost_thread -lboost_system


FROM hare1039/dockerskeleton:latest

COPY --from=builder /action/exec /action/exec
#COPY --from=builder /action/libs /action/libs/

ENV FLASK_PROXY_PORT 8080

CMD ["/bin/bash", "-c", "cd actionProxy && python -u actionproxy.py"]
