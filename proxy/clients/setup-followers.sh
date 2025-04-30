#!/bin/bash

hosts1=("172.31.15.108" "172.31.1.155" "172.31.4.10" "172.31.4.136" "172.31.8.71" "172.31.14.4" "172.31.3.50")
user="ubuntu"
ssh_key="/home/ubuntu/slsfs/proxy/slsfs.pem"  
SWARM_JOIN_TOKEN="SWMTKN-1-3ef6vaq0sn09sn5ss6o40zcbprnlvtqfri1prnkefcxw572i31-a1tvlsceludnzg3soecnse1gw"
MANAGER_IP="172.31.2.115"
OVERLAY_NETWORK="openwhisk-net"
PORT=2377

for h in "${hosts1[@]}"; do
    echo "Updating on host $h..."
    ssh -i "${ssh_key}" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null "${user}@${h}" << EOF &
        cd /home/ubuntu/slsfs/proxy || exit 1
        docker swarm join --token SWMTKN-1-04ib5lss4rvj952ba2u0l11cb58cv21p2dij2vg7c6krj5vo1e-7bsv9j4gmtgfjz2wv2z1mqaap 172.31.7.125:2377
        docker ps -q | xargs -r docker kill
        #git pull || exit 1
        #make from-docker || exit 1
EOF
done