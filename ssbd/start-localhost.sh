#!/bin/bash

#hosts1=(ssbd-1 ssbd-2 ssbd-3 ssbd-4 ssbd-5 ssbd-6 ssbd-7 ssbd-8 ssbd-9)
#hosts2=(ssbd-4 ssbd-5 ssbd-6 ssbd-7 ssbd-8 ssbd-9)

images=(hare1039/ssbd:0.0.1)

BLOCKSIZE=$1;
if [[ "$BLOCKSIZE" == "" ]]; then
    BLOCKSIZE=4096;
fi
# add the directory /tmp/haressbd

for i in "${images[@]}"; do
    docker rm -f ssbd;
    docker run --name ssbd --net=host -d -v /tmp:/tmp hare1039/ssbd:0.0.1 --listen 13000 --blocksize ${BLOCKSIZE} --db /tmp/haressbd/db1;
done

