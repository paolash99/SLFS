# slsfs-setup

## Multi Node Cluster Setup

### STEPS to perform on all nodes

#### Prerequisites

- Ubuntu (preferably 22.04)

#### Install Docker

- To install docker on your machine. You can follow the steps [here](https://docs.docker.com/engine/install/ubuntu/) or use the script `install_docker.sh`.

- use `docker run hello-world` to check if docker installation went through successfully

**Note**: If you ran the `install_docker` script, you need to log out and log back in to reflect latest changes.

#### Install JAVA

- Use the script `install_java.sh` to install the openJDK latest java version
- Run `sudo update-alternatives --config java` to list java installations
- Run `sudo vi /etc/environments`and set JAVA_HOME to the output from previous command as follows: `JAVA_HOME="/usr/lib/jvm/java-11-openjdk-amd64"`

#### Install Openwhisk

- Get the enviroment files (called moc) from (here)[vishalvrv9/slsfs-setup/moc].
- run the `openwhisk_install` script to retrieve pre-configured openwhisk. (NOTE: The rest of the script is expected to fail at this point)
- Go to `/openwhisk/ansible/environments/moc`and edit the host file with hosts required for invokers, schedulers, kafkas, etc.
  Note: If you are bringing up all of these on the same machine, the ansible host would be the machine IP and the ansible connection would be "local"
- Ensure your DB credentials are updated in `openwhisk/ansible/db_local.ini`
- Ensure the correct path to Pythonis mentioned in `openwhisk/ansible/environments/docker-machine/hosts.j2.ini` and `openwhisk/   ansible/environments/local/group_vars/all`
- Ensure you have zip installed in the machine.
- Run the openwhisk script again to successfully install openwhisk.
- This should take care of bringing up zookeeper,kafka, whisk/invoker, nginx, whisk/controller and couchdb. This can be verified using `docker ps`

#### Setup SLSFS Proxy

### Prerequisites:

1. In functions/datafunction/deploy-native.sh, add the guest openwhisk auth key as follows:
   wsk -i --apihost localhost --auth AUTH_KEY action update slsfs-datafunction-$i --native hello.zip --memory 256
2.
