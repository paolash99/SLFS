#!/bin/bash

hosts1=(ssbd-1 ssbd-2 ssbd-3 ssbd-4 ssbd-5 ssbd-6 ssbd-7 ssbd-8 ssbd-9)

for h in "${hosts1[@]}"; do
    echo ;
    echo checking ${h};
    ssh debian@${h}  docker logs ${h};
done
wait < <(jobs -p);
