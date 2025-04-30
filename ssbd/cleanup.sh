#!/bin/bash

hosts1=(ssbd-1 ssbd-2 ssbd-3 ssbd-4 ssbd-5 ssbd-6 ssbd-7 ssbd-8 ssbd-9)

for h in "${hosts1[@]}"; do
    ssh debian@"$h" "docker rm -f ${h}-1; docker rm -f ${h}-2; docker rm -f ${h}-3; sudo rm -rf /tmp/haressbd; sudo mkdir /tmp/haressbd; sudo chmod 777 /tmp/haressbd" &
done
wait < <(jobs -p);

#for i in "${images[@]}"; do
#    for h in "${hosts2[@]}"; do
#        ssh "$h" "docker rm -f ${h}; docker run --name ${h} --net=host -d -v /tmp:/tmp hare1039/ssbd:0.0.1 --blocksize ${BLOCKSIZE}; echo start $h done" &
#    done
#    wait < <(jobs -p);
#done
