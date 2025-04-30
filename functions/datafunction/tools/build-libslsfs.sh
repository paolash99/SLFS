#!/bin/bash

mkdir -p libs;

ar -qc libs/libslsfs.a slsfs.o;
ranlib libs/libslsfs.a;

libraries=(cryptopp cassandra_static rdkafka++ rdkafka uv curl ssl crypto nghttp2 z zstd brotlienc brotlidec brotlicommon boost_coroutine boost_chrono boost_thread boost_system );# stdc++ m pthread rt dl gcc_s c );
for lib in ${libraries[@]}; do
    paths=( /usr/lib/gcc/x86_64-alpine-linux-musl/10.3.1/ /usr/lib/gcc/x86_64-alpine-linux-musl/10.3.1/../../../../x86_64-alpine-linux-musl/lib/x86_64-alpine-linux-musl/10.3.1/ /usr/lib/gcc/x86_64-alpine-linux-musl/10.3.1/../../../../x86_64-alpine-linux-musl/lib/../lib/ /usr/lib/gcc/x86_64-alpine-linux-musl/10.3.1/../../../x86_64-alpine-linux-musl/10.3.1/ /usr/lib/gcc/x86_64-alpine-linux-musl/10.3.1/../../../../lib/ /lib/x86_64-alpine-linux-musl/10.3.1/ /lib/../lib/ /usr/lib/x86_64-alpine-linux-musl/10.3.1/ /usr/lib/../lib/ /usr/lib/gcc/x86_64-alpine-linux-musl/10.3.1/../../../../x86_64-alpine-linux-musl/lib/ /usr/lib/gcc/x86_64-alpine-linux-musl/10.3.1/../../../ /lib/ /usr/lib/ /usr/local/lib/ );
    for path in "${paths[@]}"; do
        if [[ -f "${path}lib${lib}.a" ]]; then
            cp "${path}lib${lib}.a" libs;
            break;
        fi
    done
done

exit 0;

mkdir arbuilder;
cd arbuilder;

libraries=(cryptopp cassandra_static rdkafka++ rdkafka uv curl ssl crypto nghttp2 z zstd brotlienc brotlidec brotlicommon boost_coroutine boost_chrono boost_thread boost_system );# stdc++ m pthread rt dl gcc_s c );
for lib in ${libraries[@]}; do
    mkdir ${lib};
    cd ${lib};

    paths=( /usr/lib/gcc/x86_64-alpine-linux-musl/10.3.1/ /usr/lib/gcc/x86_64-alpine-linux-musl/10.3.1/../../../../x86_64-alpine-linux-musl/lib/x86_64-alpine-linux-musl/10.3.1/ /usr/lib/gcc/x86_64-alpine-linux-musl/10.3.1/../../../../x86_64-alpine-linux-musl/lib/../lib/ /usr/lib/gcc/x86_64-alpine-linux-musl/10.3.1/../../../x86_64-alpine-linux-musl/10.3.1/ /usr/lib/gcc/x86_64-alpine-linux-musl/10.3.1/../../../../lib/ /lib/x86_64-alpine-linux-musl/10.3.1/ /lib/../lib/ /usr/lib/x86_64-alpine-linux-musl/10.3.1/ /usr/lib/../lib/ /usr/lib/gcc/x86_64-alpine-linux-musl/10.3.1/../../../../x86_64-alpine-linux-musl/lib/ /usr/lib/gcc/x86_64-alpine-linux-musl/10.3.1/../../../ /lib/ /usr/lib/ /usr/local/lib/ );
    for path in "${paths[@]}"; do
        if [[ -f "${path}lib${lib}.a" ]]; then
            ar -x "${path}lib${lib}.a";
            cp "${path}lib${lib}.a" .;
            break;
        fi
    done

    cd ..;
done

ar -qc ../libslsfs.a ../slsfs.o */*;

cd ..;

ranlib libslsfs.a
