
source avaliable-host.sh
export hosts=("${hosts1[@]}")
export clients=1;
export MEMO="client-$(( 1 * $clients ))"

export OUTPUT="result-test-storage-$MEMO"

bash -c 'cd ../functions/datafunction; rm -f test-storage; make test-storage;'

for h in "${hosts[@]}"; do
    ssh $h pkill test-storage;
    ssh $h rm -f /tmp/test-storage '/tmp/test-storage-output*';
    scp ../functions/datafunction/tools/ssbd.json $h:/tmp/ &
    scp ../functions/datafunction/test-storage $h:/tmp/&
done

wait < <(jobs -p);

for h in "${hosts[@]}"; do
    for client in $(seq 1 $clients); do
        ssh "$h" "bash -c 'ulimit -n 8192; /tmp/test-storage 10000 < /tmp/ssbd.json > /tmp/test-storage-output-$client.txt; echo finish $h-$client'" &
    done
done
wait < <(jobs -p);

mkdir -p $OUTPUT
cd $OUTPUT

for h in "${hosts[@]}"; do
    mkdir -p "sub-$h";
    scp "$h":'/tmp/test-storage-output*' "sub-$h/"
done
