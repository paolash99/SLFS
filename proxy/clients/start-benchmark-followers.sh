#!/bin/bash

# Define the list of remote hosts
hosts1=(f-1 f-2 f-3 f-4 f-5 f-6 f-6)

# Define the file you want to copy and the destination directory
file_to_copy="/home/ubuntu/createSubtree-1726446698173.txt"
remote_directory="/home/ubuntu"
ssh_key="/home/ubuntu/slsfs/proxy/slsfs.pem"  

# Iterate over each host and copy the file
for h in "${hosts1[@]}"; do
    echo "Copying $file_to_copy to $h:$remote_directory"
    
    # Copy the file to the remote host
    scp -i $ssh_key $file_to_copy ubuntu@"$h":"$remote_directory"
    
    # Check if the file was successfully copied
    if [ $? -eq 0 ]; then
        echo "File successfully copied to $h"
    else
        echo "Failed to copy file to $h"
    fi
done