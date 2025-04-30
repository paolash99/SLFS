
hosts1=(ow-invoker-1 ow-invoker-2 ow-invoker-3 ow-invoker-4 ow-invoker-5 ow-invoker-6 ow-invoker-7 ow-invoker-8 ow-invoker-9 ow-invoker-10 ow-invoker-11 ow-invoker-12 ow-invoker-13 ow-invoker-14 ow-invoker-15 ow-invoker-16)

images=(hare1039/slsfs-client:0.0.2)

for i in "${images[@]}"; do
    for h in "${hosts1[@]}"; do
        ssh "$h" docker rmi hare1039/slsfs-client:0.0.2;
        docker save "$i" | pv | ssh "$h" docker load &
    done
    wait < <(jobs -p);
done
