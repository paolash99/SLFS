#!/bin/bash
source start-proxy-args.sh;

TOTAL_REQUEST=100000
TOTAL_DF=4
CONCURRENT_EXECUTER=128
ENDPORT=13007
#hosts1=(proxy-1 proxy-2 proxy-3)
hosts1=(proxy-1 proxy-2 proxy-3)

TESTNAME="ssbdMOD-norepli-${#hosts1[@]}_host_${BACKEND_CONFIG_NAME}_T+${CONCURRENT_EXECUTER}_EXEC+${ENDPORT}+${TOTAL_DF}_DF"
echo "testname: $TESTNAME"

bash -c 'cd ../functions/datafunction; make function;' &
bash -c 'source start-proxy-args.sh; cd ../proxy; make from-docker; ./transfer_images.sh; ' &
bash -c "cd ../ssbd;  make from-docker; ./transfer_images.sh; ./start.sh ${BACKEND_BLOCKSIZE}" &
wait < <(jobs -p);

while curl https://localhost:10001/invokers -k 2>&1 | grep -q unhealthy; do
    echo 'waiting invoker restart'
    sleep 1;
done

echo starting;

mkdir -p "test-${TESTNAME}-df-profile-${TESTNAME}";
cd "test-${TESTNAME}-df-profile-${TESTNAME}";

for h in "${hosts1[@]}"; do
    for i in $(seq 13000 $ENDPORT); do
        ssh -t "$h" \
            docker run \
               --privileged \
               -it \
               --rm \
               --entrypoint /bin/datafunction-profile \
               --network=host \
               hare1039/transport:0.0.2 \
                   --port $i \
                   --announce $(nslookup $h | grep -v '127' | grep Address | awk '{ print $2 }') \
                   --worker-config $BACKEND_CONFIG \
                   --total-request $TOTAL_REQUEST \
                   --total-df      $TOTAL_DF \
                   --concurrent-executer $CONCURRENT_EXECUTER 2>&1 | \
            tee pxy_${h}_${i}.txt &
    done
done
wait
