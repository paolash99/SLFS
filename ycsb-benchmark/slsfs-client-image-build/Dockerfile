FROM ubuntu:23.04

RUN apt-get update && apt-get install -y --no-install-recommends gdb

ADD libzookeeper_mt.so.2 /lib/libzookeeper_mt.so.2
ADD librt.so.1           /lib/librt.so.1
ADD slsfs-client-dynamic /bin/slsfs-client-dynamic
ADD slsfs-client-ddf     /bin/slsfs-client-ddf
ADD slsfs-cmd            /bin/slsfs-cmd
ADD slsfs-trace-emulator /bin/slsfs-trace-emulator
ADD slsfs-iozone         /bin/slsfs-iozone
ADD ceph-iozone          /bin/ceph-iozone
