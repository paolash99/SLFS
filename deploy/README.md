# slsfs-setup

## Multi Node Cluster Setup

### STEPS to perform on all nodes

#### Prerequisites
- Ubuntu (preferably 22.04)

### Install Docker
- To install docker on your machine. You can follow the steps [here](https://docs.docker.com/engine/install/ubuntu/) or use the script `install_docker.sh`. 

- use `docker run hello-world` to check if docker installation went through successfully 

**Note**: If you ran the `install_docker` script, you need to log out and log back in to reflect latest changes.

### Install JAVA
- Use the script `install_java.sh` to install the openJDK latest java version
- Run `sudo update-alternatives --config java` to list java installations
- Run `sudo vi /etc/environments`and set JAVA_HOME to the output from previous command as follows: `JAVA_HOME="/usr/lib/jvm/java-11-openjdk-amd64"`

### Install Openwhisk

- get the enviroment files (called moc) from (here)[vishalvrv9/slsfs-setup/moc].
- run the `openwhisk_install` script to retrieve pre-configured openwhisk. (NOTE: The rest  of the script is expected to fail at this point)
- go to `/openwhisk/ansible/environments/moc`and edit the host file with hosts required for invokers, schedulers, kafkas, etc.
- run the openwhisk script again to successfully install openwhisk

### Setup a Docker Overlay Network

A Docker overlay network would be required to establish communication between standalone containers on different Docker daemons using an overlay network.

[This Offical Docker Tutorial](https://docs.docker.com/network/network-tutorial-overlay/#use-an-overlay-network-for-standalone-containers) helps achieve that. 


#### Tutorial for setting up 3 node version of slsfs with openwhisk

- In the 3 node version, we setup a openwhisk cluster with 1 controller and 2 invoker nodes.
- To configure the cluster, we use the hosts file present in this folder.
- Update the IP/hosts with the IP/hosts of the different components of the cluster as required.

#### Configurations to be fixed in a single node setup:
1. Update the /openwhisk/ansible/environments/moc/hosts with appropriate IPs of controller, kafka, scheduler, zookeeper, invoker and db. All should be using the IP of the single node with ansible_connection set to local. Refer the "Install Openwhisk" section above.
2. Ensure the controller VM has the following running upon running deploy/openwhisk_install.sh:
   - nginx
   - controller
   - kafka
   - zookeeper
   - couchDB
3. In the deployed openwhisk/ansible/db_local.ini, ensure the db details are updated.
4. Ensure you have atleast 1 invoker, 1 proxy, 1 ssbd, 1 client and 1 invoker running within the VM.
5. Ensure you update the proxy/trigger.sh to use VM IP [here](../proxy/trigger.hpp#L240)
6. Ensure the proxy/zookeeper.hpp points to the localhost IP [here](../proxy/zookeeper.hpp#L70)

#### Configurations to be fixed in a multinode setup:
1. Update the /openwhisk/ansible/environments/moc/hosts with appropriate IPs of controller, kafka, scheduler, zookeeper, invoker and db. Refer the "Install Openwhisk" section above.
2. In the deployed openwhisk/ansible/db_local.ini, ensure the db details are updated.
3.  Ensure you have atleast 1 invoker, 1 proxy, 1 ssbd, 1 client and 1 invoker running within individual VMs. To best test the distributed setup, ensure you have more of each of these all running on different VMs.
4. Ensure the controller VM is able to communicate with all the other VMs. This can be taken care by ensuring you import the controller SSH keys while bringing up the other VMs.
5. The proxy Dockerfile takes care of ensuring the private key is loaded into each proxy container to ensure inter-proxy communication is possible in case of proxy failure.
6. Ensure you update the proxy/trigger.sh to use controller IP [here](../proxy/trigger.hpp#L240)
7. Ensure the proxy/zookeeper.hpp points to the right zookeeper IP [here](../proxy/zookeeper.hpp#L70)
   

##### Setup SLSFS Proxy

- Build the proxy
```
cd ~/slsfs/proxy
make from-docker
```
- Build ssbd
```
cd ~/slsfs/ssbd
make from-docker
```
- Build datafunction
```
cd ~/slsfs/functions/datafunction
make from-docker
```
- Start proxy (to change configurations of proxy before starting, look at start-proxy-args-template.sh)
```
~/slsfs/ycsb-benchmark
./start-proxy-args.sh
```
OR

```
~/slsfs/proxy
./start-proxy.sh
./start-proxy-new.sh
```
This will bring up 2 proxies. Ensure to update the host, name and port details in each.


- Run the ycsb-benchmark
```
cd ~/slsfs/ycsb-benchmark
# to run direct data function test
./run-slsfs-client-direct-test.sh
# to run dynamic proxy test
./run-slsfs-client-dynamic-test.sh
```

For debugging, its easier to run commands in ~/slsfs/ycsb-benchmark/run-slsfs-client-dynamic-test.sh individually. For reference to initiate client requests, you can use:
```
ulimit -n 8192; docker run --name slsfs-client --rm -v /tmp:/tmp  hare1039/slsfs-client:0.0.2  slsfs-client-dynamic --zookeeper zk://192.168.0.52:2181 --total-times 500 --total-clients 1 --total-duration 2000 --bufsize 4096 --zipf-alpha 1.2 --uniform-dist --result /tmp/invoker0-runtest-direct-size-1_ssbd-27_T+0-100_H+1_TH+1 --test-name 0-100![image](https://github.com/hare1039/slsfs/assets/132250274/708eeec1-d78a-45cd-bd95-e498e0e8f985)
```
You should be able to see the requests to proxy logs and how many requests were finished, along with the throughput , on running this command. You can also check proxy logs to see the workers being brought up.


#### Changing benchmark configurations

The above scripts run different pre-configured benchmarks. In order to change the proxy configurations, we can tweak the ```start-proxy-args.sh```. The different variables available are:

- EACH_CLIENT_ISSUE : The number of requests each client issues
- TOTAL_CLIENT : Total number of clients
- TOTAL_TIME_AVAILABLE : Time until the client is up and running
- ENABLE_DIRECT_CONNECT : To enable direct connection between client and datafunction
- POLICY_FILETOWORKER : How the proxy assigns datafunction to client
- ENABLE_CACHE : To enable caching 

#### Experiments

The experiments from the SLFS paper can be replicated to test and verify the benchmarks as below:

- Figure 6: Placing blocks of the same file in the same PSB
node vs. scattering them across different PSB nodes.
  - To run this experiment, we need to run the ```~/slsfs/ycsb-benchmark/run-slsfs-client-test.sh``` with specific proxy arguments

  - The ```BACKEND_CONFIG``` variable inside ```start-proxy-args.sh``` should point to specific backend configs:
    - ```/proxy/backend/ssbd-27-repl-none.json``` : to run experiments with blocks of same file going to the same PSB
    - ```/proxy/backend/ssbd-27-repl-2.json``` : to run experiments with blocks of same size getting scattered across different PSB nodes. This config creates 3 replicas of PSB nodes.
