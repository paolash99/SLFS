#!/bin/bash

# Define swarm join command
SWARM_JOIN_TOKEN="SWMTKN-1-3ef6vaq0sn09sn5ss6o40zcbprnlvtqfri1prnkefcxw572i31-a1tvlsceludnzg3soecnse1gw"
MANAGER_IP="172.31.2.115:2377"

# Define the list of ssbd machines and image to deploy
ssbd_hosts=(ssbd-1 ssbd-2 ssbd-3 ssbd-4 ssbd-5 ssbd-6 ssbd-7 ssbd-8 ssbd-9)
images=(hare1039/ssbd:0.0.1)

# Set block size from the first argument, or default to 4096 if not provided
BLOCKSIZE=${1:-4096}

# Define the SSH key location
ssh_key="/home/ubuntu/slsfs/proxy/slsfs.pem"

# Make each ssbd machine join the Docker Swarm and deploy containers on openwhisk-net
for host in "${ssbd_hosts[@]}"; do
     # Join Docker Swarm on each ssbd machine
     echo "Joining ${host} to Docker Swarm..."

     ssh -i "${ssh_key}" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null ubuntu@"$host" \
         "docker swarm join --token SWMTKN-1-04ib5lss4rvj952ba2u0l11cb58cv21p2dij2vg7c6krj5vo1e-7bsv9j4gmtgfjz2wv2z1mqaap 172.31.7.125:2377"

    # Ensure /tmp/haressbd directory exists on each machine
    echo "Setting up directories on ${host}..."
    ssh -i "${ssh_key}" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null ubuntu@"$host" \
        "sudo rm -rf /tmp/haressbd; sudo mkdir -p /tmp/haressbd"

    # Deploy containers on openwhisk-net for each host with the specified block size
    echo "Deploying containers on ${host} with block size ${BLOCKSIZE}..."
    ssh -i "${ssh_key}" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null ubuntu@"$host" \
        "docker rm -f ${host}-1; docker run --name ${host}-1 --network=openwhisk-net -p 13000:13000 -d -v /tmp:/tmp hare1039/ssbd:0.0.1 --listen 13000 --blocksize ${BLOCKSIZE} --db /tmp/haressbd/db1; echo start ${host}-1 done" &
    ssh -i "${ssh_key}" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null ubuntu@"$host" \
        "docker rm -f ${host}-2; docker run --name ${host}-2 --network=openwhisk-net -p 13001:13001 -d -v /tmp:/tmp hare1039/ssbd:0.0.1 --listen 13001 --blocksize ${BLOCKSIZE} --db /tmp/haressbd/db2; echo start ${host}-2 done" &
    ssh -i "${ssh_key}" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null ubuntu@"$host" \
        "docker rm -f ${host}-3; docker run --name ${host}-3 --network=openwhisk-net -p 13002:13002 -d -v /tmp:/tmp hare1039/ssbd:0.0.1 --listen 13002 --blocksize ${BLOCKSIZE} --db /tmp/haressbd/db3; echo start ${host}-3 done" &
done

# Wait for all background jobs to finish
wait < <(jobs -p)

echo "All ssbd machines have joined the Swarm and containers have been deployed on openwhisk-net."