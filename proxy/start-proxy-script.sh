UUID=$1
IP=$2
PORT=$3
NAME=$4

docker rm -f $NAME;
docker run -v /var/run/docker.sock:/var/run/docker.sock --privileged -d \
       --name=$NAME \
       --net=host \
       --volume=/tmp:/tmp \
       hare1039/transport:0.0.2 \
           --listen $PORT \
           --announce $IP \
           -v \
           --enable-direct-connection \
           --server-id                $UUID \
           --report                   "/tmp/proxy-report.json" \
           --policy-filetoworker      "active-load-balance" \
           --policy-filetoworker-args "" \
           --policy-launch            "max-queue" \
           --policy-launch-args       10:400 \
           --policy-keepalive         "moving-interval-global" \
           --policy-keepalive-args    5:180000:1000:50 \
           --worker-config            "/backend/ssbd-27.json" \
           --max-function-count       15 \
           --blocksize                4096 \
           --enable-cache \
           --cache-size               113 \
           --cache-policy             "LRU"