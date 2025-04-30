# For direct communication
docker run --rm -it \
    --entrypoint '' \
    --net=openwhisk-net \
    --ip 10.0.1.236 \
    hare1039/transport:0.0.2 \
    java -Dsun.io.serialization.extendedDebugInfo=true -Xmx1g -Xms1g -XX:+UseConcMarkSweepGC -XX:+UnlockDiagnosticVMOptions \
    -XX:ParGCCardsPerStrideChunk=512 -XX:+CMSScavengeBeforeRemark -XX:MaxGCPauseMillis=350 -XX:MaxTenuringThreshold=2 \
    -XX:MaxNewSize=1000m -XX:+CMSClassUnloadingEnabled -XX:+ScavengeBeforeFullGC \
    -jar /bin/slsfs-benchmark.jar --leader_ip 10.0.1.236 --leader_port 8000 --zookeeper3 zk://172.31.6.209:2183 --zookeeper2 zk://172.31.8.178:2182 --zookeeper zk://172.31.5.243:2181 --num_followers 7 --input_path /createSubtree.txt 

# docker run --rm -it \
#     --entrypoint '' \
#     --net=host \
#     hare1039/transport:0.0.2 \
#     java -Dsun.io.serialization.extendedDebugInfo=true -Xmx1g -Xms1g -XX:+UseConcMarkSweepGC -XX:+UnlockDiagnosticVMOptions \
#     -XX:ParGCCardsPerStrideChunk=512 -XX:+CMSScavengeBeforeRemark -XX:MaxGCPauseMillis=350 -XX:MaxTenuringThreshold=2 \
#     -XX:MaxNewSize=1000m -XX:+CMSClassUnloadingEnabled -XX:+ScavengeBeforeFullGC \
#     -jar /bin/slsfs-benchmark.jar --leader_ip 172.31.11.96 -n --leader_port 8000 --zookeeper3 zk://172.31.5.26:2181 --zookeeper2 zk://172.31.5.26:2181 --zookeeper zk://172.31.5.26:2181 -n --num_followers 7 --input_path /createSubtree.txt 