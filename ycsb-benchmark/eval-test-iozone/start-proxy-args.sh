#!/bin/bash

source avaliable-host.sh
export hosts=("${hosts1[@]}")

export EACH_CLIENT_ISSUE=50000
export TOTAL_CLIENT=4
export TOTAL_TIME_AVAILABLE=10000

export QSIZE=1
export BUFSIZE=$(( 4096 * $QSIZE ))
export UNIFORM_DIST="--uniform-dist"


#change here
export MEMO="iozone-re-$QSIZE"

##### ----- proxy args ----- #####

export BACKEND_CONFIG=/backend/ssbd-27.json
#export BACKEND_CONFIG=/backend/cassandra-repl3.json
export BACKEND_CONFIG_NAME=$(echo ${BACKEND_CONFIG} | sed 's/\/backend\///g' | sed 's/.json//g')
export BACKEND_BLOCKSIZE=4096

# [random-assign, lowest-load, active-load-balance]

export POLICY_FILETOWORKER=active-load-balance
export POLICY_FILETOWORKER_ARGS=""

# [const-average-load]
export POLICY_LAUNCH=max-queue
export POLICY_LAUNCH_ARGS=10:400 #average queue = 2kb


# [const-time, moving-interval]
#export POLICY_KEEPALIVE=const-time
#export POLICY_KEEPALIVE_ARGS=100000
export POLICY_KEEPALIVE=moving-interval-global
export POLICY_KEEPALIVE_ARGS=5:180000:1000:50


# export ENABLE_CACHE="--enable-cache"
export ENABLE_CACHE=""
export CACHE_SIZE="113" #MB
export CACHE_POLICY="LRU"

export NEW_CLUSTER="--new-cluster";
export VERBOSE='-v'
export MAX_FUNCTION_COUNT=15
export PORT=12001

export ENABLE_DIRECT_CONNECT=""

### the set of ddf
# export ENABLE_DIRECT_CONNECT="--enable-direct-connection"
# export POLICY_FILETOWORKER=random-assign
# export POLICY_FILETOWORKER_ARGS=""
# export POLICY_LAUNCH=fix-pool
# export POLICY_LAUNCH_ARGS=10:4 # 10 pending request; maintain 4 functions
### endset

start-proxy-remote()
{
    local h=$1;
    ssh "$h" docker rmi hare1039/transport:0.0.2;
    docker save hare1039/transport:0.0.2 | pv | ssh "$h" docker load;
    scp start-proxy* avaliable-host.sh $h:

    if [[ "$h" == "proxy-1" ]]; then
        ssh $h "echo 'export SERVER_ID=0' >> ./start-proxy-args.sh"
    elif [[ "$h" == "proxy-2" ]]; then
        ssh $h "echo 'export SERVER_ID=0.66' >> ./start-proxy-args.sh"
    elif [[ "$h" == "proxy-3" ]]; then
        ssh $h "echo 'export SERVER_ID=0.32' >> ./start-proxy-args.sh"
    elif [[ "$h" == "zookeeper-1" ]]; then
        ssh $h "echo 'export SERVER_ID=0.49' >> ./start-proxy-args.sh"
    elif [[ "$h" == "zookeeper-2" ]]; then
        ssh $h "echo 'export SERVER_ID=0.16' >> ./start-proxy-args.sh"
    elif [[ "$h" == "zookeeper-3" ]]; then
        ssh $h "echo 'export SERVER_ID=0.84' >> ./start-proxy-args.sh"
    fi

    if [[ "$2" == "noinit" ]]; then
        ssh $h "echo 'NEW_CLUSTER=\"\"' >> ./start-proxy-args.sh"
    fi
    ssh $h "/home/ubuntu/start-proxy.sh"
}
