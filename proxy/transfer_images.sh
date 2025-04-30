hosts1=(proxy-1 proxy-2 proxy-3 zookeeper-1 zookeeper-2 zookeeper-3)

images=(hare1039/transport:0.0.2)

for i in "${images[@]}"; do
    for h in "${hosts1[@]}"; do
        docker save "$i" | pv | ssh "$h" docker load &
    done
    wait < <(jobs -p);
done
