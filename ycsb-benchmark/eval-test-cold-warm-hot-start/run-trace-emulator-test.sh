#!/bin/bash
source start-proxy-args.sh;

echo "Username: $1";

TESTNAME="${BACKEND_CONFIG_NAME}_T+${1}_P+${POLICY_FILETOWORKER}+${POLICY_LAUNCH}+${POLICY_KEEPALIVE}"
echo "testname: $TESTNAME"

ssh proxy-1 docker rm -f proxy2&
ssh proxy-2 docker rm -f proxy2&
ssh proxy-3 docker rm -f proxy2&

bash -c 'cd ../functions/datafunction; make function;' &
bash -c 'source start-proxy-args.sh; cd ../proxy; make from-docker; ./transfer_images.sh' &
bash -c "cd ../ssbd; make from-docker; ./transfer_images.sh; ./cleanup.sh; ./start.sh ${BACKEND_BLOCKSIZE}" &
bash -c "cd ../../soufiane/serverlessfs/bench/trace-emulator; make from-docker; "&
wait < <(jobs -p);

echo starting remote hosts

start-proxy-remote proxy-1 &
start-proxy-remote proxy-2 noinit &
start-proxy-remote proxy-3 noinit &

hostparis=("proxy-1:12001"
           "proxy-2:12001"
           "proxy-3:12001")

bash -c "cd slsfs-client-image-build; ./build.sh; ./transfer_images.sh" &

wait < <(jobs -p);

while curl https://localhost:10001/invokers -k 2>&1 | grep -q unhealthy; do
    echo 'waiting invoker restart'
    sleep 1;
done

echo wait until proxy open

for pair in "${hostparis[@]}"; do
    p=(`echo $pair | tr ':' ' '`)
    while ! nc -z -v -w1 ${p[0]} ${p[1]} 2>&1 | grep -q succeeded; do
        echo "waiting $pair";
        sleep 1;
    done
done

wait < <(jobs -p);

mkdir -p trace_emulator_rerun_new/$TESTNAME-result;
mkdir -p trace_emulator_rerun_new/$TESTNAME-result/activation-list/;

start-watching-ow()
{
    for i in {1..10000}; do
        wsk -i activation list --limit 200 --skip 0 > trace_emulator_rerun_new/$TESTNAME-result/activation-list/ow-activation-list-$i.txt;
        sleep 60;
    done
}

echo starting;

start-watching-ow &
OW_WATCHING=$!

ssh ow-invoker-1

mkdir -p trace-emulator-test/$TESTNAME-result/
cd "trace-emulator-test/$TESTNAME-result/";

cp ../../start-proxy-args.sh .
scp proxy-1:/tmp/proxy-report.json proxy-report-1.json;
ssh proxy-1 docker logs proxy2 2>  docker-log-ow-invoker-1.backup.txt;
scp ow-invoker-1:report.csv "report.csv"
cat activation-list/*.txt \
    | awk '!seen[$0]++' > "activation-list.txt" && \
    rm -r activation-list;

kill $OW_WATCHING >/dev/null 2>&1

wait < <(jobs -p);

echo -e "\a finish test: $TESTNAME"
