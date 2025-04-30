IP=$(ip -4 addr show ens5 | grep -oP '(?<=inet\s)\d+(\.\d+){3}' | head -n 1)
#if [[ -z "$SERVER_ID" ]]; then
#    SERVER_ID=-1;
#fi

#           -ex=r \
#               --args /bin/slsfs-proxy \
#       --entrypoint gdb -it \


docker rm -f proxy-1;
docker run -v /var/run/docker.sock:/var/run/docker.sock --privileged -d \
   --name=proxy-1 \
   --network=openwhisk-net \
   -p 12003:12003 \
       --volume=/tmp:/tmp \
       hare1039/transport:0.0.2 \
       --listen 12003 \
       --announce $IP \
       --new-cluster \
       --enable-direct-connection \
       --server-id 0.00 \
       --report "/tmp/proxy-report.json" \
       --policy-filetoworker "active-load-balance" \
       --policy-filetoworker-args "" \
       --policy-launch "fix-pool" \
       --policy-launch-args 10:15 \
       --policy-keepalive "moving-interval-global" \
       --policy-keepalive-args 5:180000:1000:50 \
       --worker-config "/backend/ssbd-27.json" \
       --max-function-count 15 \
       --blocksize 4096 \
       --enable-cache \
       --cache-size 113 \
       --cache-policy "LRU"


# docker run -v /var/run/docker.sock:/var/run/docker.sock --privileged -d \
#    --name=proxy-1 \
#    --network=host \
#        --volume=/tmp:/tmp \
#        hare1039/transport:0.0.2 \
#        --listen 12003 \
#        --announce $IP \
#        --new-cluster \
#        --server-id 0.00 \
#        --report "/tmp/proxy-report.json" \
#        --policy-filetoworker "active-load-balance" \
#        --policy-filetoworker-args "" \
#        --policy-launch "fix-pool" \
#        --policy-launch-args 10:15 \
#        --policy-keepalive "moving-interval-global" \
#        --policy-keepalive-args 5:180000:1000:50 \
#        --worker-config "/backend/ssbd-27.json" \
#        --max-function-count 15 \
#        --blocksize 4096 \
#        --enable-cache \
#        --cache-size 113 \
#        --cache-policy "LRU"