#!/bin/bash

# Variables
MANAGER_IP="172.31.2.115"
OVERLAY_NETWORK="openwhisk-net"
PORT=2377
hosts1=("172.31.5.243" "172.31.8.178" "172.31.6.209" "172.31.5.113" "172.31.8.75" "172.31.6.104" "172.31.12.36" "172.31.15.162" "172.31.14.130" "172.31.15.161" "172.31.6.221" "172.31.11.156" "172.31.13.123" "172.31.11.123" "172.31.3.154" "172.31.1.24" "172.31.13.55" "172.31.3.183" "172.31.9.215")
user="ubuntu"
ssh_key="/home/ubuntu/slsfs/proxy/slsfs.pem"
SWARM_JOIN_TOKEN="SWMTKN-1-3ef6vaq0sn09sn5ss6o40zcbprnlvtqfri1prnkefcxw572i31-a1tvlsceludnzg3soecnse1gw"

# Loop through each IP in hosts1
for i in "${!hosts1[@]}"; do
  ip="${hosts1[$i]}"
  
  echo "Connecting to $ip..."
  
  # SSH into the machine and execute the commands
  ssh -i "$ssh_key" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null "$user@$ip" <<EOF
    docker kill test-$i
    docker kill test-paola-$i
    echo "Cleaned up containers starting with 'test-alpine'."
EOF
  
  echo "Operations completed on $ip."
done