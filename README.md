# SLFS

**SLFS** (ServerLess File System) is a distributed file system built entirely on serverless functions. While serverless computing is typically used for stateless, short-lived applications, SLFS challenges this norm by enabling actual file system operations via serverless functions.

SLFS is built atop distributed key-value stores and supports both cloud-based and custom storage backends. By leveraging the elasticity of serverless platforms, SLFS offers dynamic scaling of file operations while maintaining competitive performance, scalability, and cost-efficiency when compared to traditional server-based distributed file systems.

---

## Getting Started

Follow the steps below to deploy and run SLFS.

---

### 1. Set Up OpenWhisk

1. Launch EC2 instances:
   - One instance with the **Worker AMI** for each worker node.
   - One instance with the **Worker AMI** for each ZooKeeper node.
   - One instance with the **Controller AMI** for the controller node.

2. SSH into the **commander node** and perform the following:
   - Update the IPs in:
     - `/slsfs/deploy/openwhisk/ansible/environments/moc/hosts`
     - `/slsfs/deploy/openwhisk/ansible/db_local.ini`
   - Navigate to `/slsfs/deploy` and run:
     ```bash
     ./openwhisk_install.sh
     ```

3. Once OpenWhisk is up and running:
   - SSH into one of the **worker nodes**.
   - Update `/etc/hosts` to include the controller node’s IP as `ow-ctrl`.
   - Navigate to `/slsfs/functions/datafunction` and run:
     ```bash
     ./deploy-native.sh
     ```

---

### 2. Set Up the Backend

- Launch an EC2 instance with the **SSBD AMI** for each backend node.

---

### 3. Set Up the Proxy

Repeat all steps below for each proxy node (skip step `c` for the second and third proxies).

1. SSH into the proxy node.
2. Update `/etc/hosts` with the IPs of all instances.
3. (Only for the first proxy) Navigate to `/slsfs/ssbd` and run:
   ```bash
   ./start-localhost.sh
   ```
4. Update `/slsfs/proxy/backend/ssbd-27.json` with the backend nodes’ IPs.
5. Rebuild the proxy Docker image:
   ```bash
   cd /slsfs/proxy
   make from-docker
   ```
6. Start the proxy:
   - First proxy:
     ```bash
     ./start_proxy.sh
     ```
   - Second proxy:
     ```bash
     ./start_proxy-new.sh
     ```
   - Third proxy:
     ```bash
     ./start_proxy-new-2.sh
     ```

---

### 4. Format the Filesystem

1. Run the SLFS command-line tool using Docker:
   ```bash
   docker run -it hare1039/slsfs-client:0.0.2 slsfs-cmd --zookeeper zk://<zookeeper0-IP>:2181
   ```
2. Inside the CLI, run:
   ```bash
   init /
   ```
3. Use this CLI to explore and test SLFS functionality.
