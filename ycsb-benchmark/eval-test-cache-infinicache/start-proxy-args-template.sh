#!/bin/bash

source avaliable-host.sh
export hosts=("${hosts16[@]}")

export EACH_CLIENT_ISSUE=2000000
export TOTAL_CLIENT=16
export TOTAL_TIME_AVAILABLE=200

export QSIZE=QSIZE_ARGS
export QTEST=QTEST_ARGS
export BUFSIZE=$(( 4096 * $QSIZE ))
export UNIFORM_DIST="--uniform-dist"
#export UNIFORM_DIST=""

export MEMO="createwrite-size-$QSIZE-test-$QTEST"

#export CLIENT_TESTNAME=100-0
#export CLIENT_TESTNAME=fill
#export CLIENT_TESTNAME=100-0
export CLIENT_TESTNAME=0-100
#export CLIENT_TESTNAME=samename
#export CLIENT_TESTNAME=samename

#export UPLOAD_GDRIVE=11XxuGx1nAAUyJBq1e-Gltdml0h6UJDKWEKe_1CgxVoU
#export UPLOAD_GDRIVE=1J4ZMcP0RF6zGHzocOtFMgNe8G92TPPG5Y1oE5Z7UyeI
#export UPLOAD_GDRIVE=1luyQR39ALkms4cvKTsmQO1nVrQhDF-P8IDIQ9f8jDuU
#export UPLOAD_GDRIVE=1lIPaPVSvfBi0ArjKag6Z-0spgltrRo1Yk2bJ3pgc7So #New best backend
#export UPLOAD_GDRIVE=1e0PVLkncIAxI1XiSjVIcGunEvAwKsBAx23ZiQUhzdxI #Block size
#export UPLOAD_GDRIVE=1J4ZMcP0RF6zGHzocOtFMgNe8G92TPPG5Y1oE5Z7UyeI #repl
#export UPLOAD_GDRIVE=1luyQR39ALkms4cvKTsmQO1nVrQhDF-P8IDIQ9f8jDuU #ceph
#export UPLOAD_GDRIVE=1pApoEAeNjSc2zx_VSOvN0DfF4e_X8oX6n3DnKW9FB1g
#export UPLOAD_GDRIVE=1hSNHYNEQEh0MsqcPGYtKwDlxAI29vM4mwJ31cZydqqw
#export UPLOAD_GDRIVE=13v7F8u5T4oTz5y2FouoM_rOZlsJ5xpRRIzLriEz55K4
#export UPLOAD_GDRIVE=1bXijTlCXewz5uCihQDKa1LYhEZ0f-CtIbW5hcxUoYXo #same vs scattered
#export UPLOAD_GDRIVE=1qn_DTIzNEyWs4WxnToti2FJp73Th78eik4JtjnxpWGo #Scala
#export UPLOAD_GDRIVE=1lwRVGAiX_81rkk1Ml7iBTOVm_iiqGxfIUTdu4hzaMiY #Direct (request per function)
#export UPLOAD_GDRIVE=1L81OCqWnoEQGVNsrJ4iCv3Qg_IUFtkwNdcKFKMGW63Q
#export UPLOAD_GDRIVE=1G0E_2yFEF4ZIh3F3mi2Rsy9IBK7W7QpYV4Vjsd2fgd4   #proxy-compare
#export UPLOAD_GDRIVE=1KNYWYCxvLO7jDQ208bHGBkBl1KDV777VcOaqAYOw3Lc  #Replica Compare
export UPLOAD_GDRIVE=11xXR56mAy8osxR8jL7hnhWnQ98F7M_ZaDycD-xkAG0Y  #Create file compare

##### ----- proxy args ----- #####

#export BACKEND_CONFIG=/backend/cassandra-repl3.json
#export BACKEND_CONFIG=/backend/ssbd-basic-async.json
#export BACKEND_CONFIG=/backend/ssbd.json
export BACKEND_CONFIG=/backend/ssbd-27.json           #normal
#export BACKEND_CONFIG=/backend/ssbd-27-repl-none.json #replica=0
#export BACKEND_CONFIG=/backend/ssbd-27-repl-2.json    #replica=1
#export BACKEND_CONFIG=/backend/ssbd-repl-none.json
#export BACKEND_CONFIG=/backend/ssbd-repl1.json
#export BACKEND_CONFIG=/backend/ssbd-repl-none.json
#export BACKEND_CONFIG=/backend/ssbd-debug.json
#export BACKEND_CONFIG=/backend/ssbd-single.json
#export BACKEND_CONFIG=/backend/ssbd-stripe.json
#export BACKEND_CONFIG=/backend/ssbd-basic.json
#export BACKEND_CONFIG=/backend/swift.json

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
export POLICY_KEEPALIVE_ARGS=5:4000:1000:50

export INITINT=1;
export VERBOSE='-vvv'
export MAX_FUNCTION_COUNT=15
export PORT=12001

start-proxy-remote()
{
    local h=$1;
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
        ssh $h "echo 'INITINT=0' >> ./start-proxy-args.sh"
    fi
    ssh $h "/home/ubuntu/start-proxy.sh"
}
