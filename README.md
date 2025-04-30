
# SLFS

Serverless computing frameworks are traditionally suited for stateless, short-lived applications
that depend on various external services for managing complex states. However, the potential of
serverless frameworks to accommodate a broader range of applications has sparked a lot of
interest. This includes using the serverless paradigm for backend systems typically reserved for
serverful infrastructure.

**SLFS** is the first distributed file system designed using serverless functions to perform actual file
system operations. It operates atop distributed key-value stores, offering users the flexibility to
choose cloud-provided or custom storage solutions. By leveraging the elasticity of serverless
functions, SLFS dynamically scales file operations. It provides a serverless solution that rivals
traditional server-based distributed file systems in cost-efficiency, performance, and scalability.

To run SLFS follow the below steps:
1. Set up OpenWhisk
    a. Launch an EC2 instance with the Worker AMI for each worker node.
    b. Launch an EC2 instance with the Worker AMI for each zookeeper node (same AMI
    as the worker’s).
    c. Launch an EC2 instance with the Controller AMI for the controller node.
    d. Connect to the commander node via ssh.
    e. Modify the IPs (only) with IPs of the new machines in
    /slsfs/deploy/openwhisk/ansible/environments/moc/hosts and
    /slsfs/deploy/openwhisk/ansible/db_local.ini.
    f. Navigate to /slsfs/deploy and run ./openwhisk_install.sh
    g. Wait until OpenWhisk is up and running the connect to one of the worker nodes
    vias ssh.
    h. Update the ow-ctrl IP in /etc/hosts with the controller node’s IP.
    i. Navigate to /slsfs/functions/datafunction and run ./deploy-native.sh
2. Set up the backend
    a. Launch an EC2 instance with the SSBD AMI for each backend node.
3. Set up the proxy (repeat all steps for the other proxies except for step c)
    a. Connect to the proxy node via ssh.
    b. Update the IPs in /etc/hosts with all the instances IPs.
    c. Navigate to /slsfs/ssbd and run ./start-localhost.sh
    d. Update the IPs in /slsfs/proxy/backend/ssbd-27.json with backend nodes’ IP
    addresses.
    e. Navigate to /slsfs/proxy and run “make from-docker” to rebuild the proxy image.
    f. Once the image is built, run “./start_proxy.sh” (or “./start_proxy-new.sh” for the
    second proxy and “./start_proxy-new-2.sh” for the third proxy).
4. Format the filesystem
    a. Run the cmd test program to create the base directory with “docker run -it
    hare1039/slsfs-client:0.0.2 slsfs-cmd --zookeeper zk://<zookeeper0 IP>:2181”
    b. While the program is running enter the command: “init /”.
    c. Use the same program to explore and test SLFS functionality.