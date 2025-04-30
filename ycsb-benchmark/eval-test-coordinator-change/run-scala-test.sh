#!/bin/bash
source start-proxy-args.sh;

TESTNAME="${MEMO}4_${BACKEND_CONFIG_NAME}_T+${CLIENT_TESTNAME}_H+${#hosts[@]}_TH+${TOTAL_CLIENT}"
echo "testname: $TESTNAME"

ssh proxy-1 docker rm -f proxy2&
ssh proxy-2 docker rm -f proxy2&
ssh proxy-3 docker rm -f proxy2&

bash -c 'cd ../functions/datafunction; make function;' &
bash -c 'source start-proxy-args.sh; cd ../proxy; make from-docker; ./transfer_images.sh; cd -; start-proxy-remote proxy-1; start-proxy-remote proxy-2 noinit; start-proxy-remote proxy-3 noinit' &
bash -c "cd ../ssbd;  make from-docker; ./transfer_images.sh; ./cleanup.sh; ./start.sh ${BACKEND_BLOCKSIZE}" &
wait < <(jobs -p);

hostparis=("proxy-1:12001"
           "proxy-2:12001"
           "proxy-3:12001");

rm -f /tmp/slsfs-client;
docker run --rm --entrypoint cat hare1039/transport:0.0.2 /bin/slsfs-client > /tmp/slsfs-client;
chmod +x /tmp/slsfs-client

for h in "${hosts[@]}"; do
    ssh $h rm -f /tmp/slsfs-client;
    scp /tmp/slsfs-client "$h":/tmp/slsfs-client;
done
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

echo starting;

start-test()
{
    local WAITTIME=$1;
    local EACH_CLIENT_ISSUE_LOCAL=$2;
    local TOTAL_CLIENT_LOCAL=$3;
    local TOTAL_TIME_AVAILABLE_LOCAL=$4;
    local TESTNAME_LOCAL=$5;

    echo "wait $WAITTIME"
    sleep $WAITTIME;
    echo "starting @$WAITTIME"

    for h in "${hosts[@]}"; do
        ssh "$h" "bash -c 'ulimit -n 8192; /tmp/slsfs-client --total-times ${EACH_CLIENT_ISSUE_LOCAL} --total-clients ${TOTAL_CLIENT_LOCAL} --total-duration ${TOTAL_TIME_AVAILABLE_LOCAL} --bufsize $BUFSIZE --zipf-alpha 1.1 ${UNIFORM_DIST} --result /tmp/$h-$TESTNAME_LOCAL --test-name $CLIENT_TESTNAME'" &
    done

    echo "waiting test @$WAITTIME to finish";
    wait;
    echo "test @$WAITTIME finished";
}

for h in "${hosts[@]}"; do
    ssh "$h" "rm -f /tmp/$h-$TESTNAME*";
done

export TOTAL_CLIENT=2
export TOTAL_TIME_AVAILABLE=60
start-test  0 $EACH_CLIENT_ISSUE $TOTAL_CLIENT $TOTAL_TIME_AVAILABLE ${TESTNAME}-part1 ${CLIENT_TESTNAME} &

export TOTAL_CLIENT=4
export TOTAL_TIME_AVAILABLE=40
start-test 20 $EACH_CLIENT_ISSUE $TOTAL_CLIENT $TOTAL_TIME_AVAILABLE ${TESTNAME}-part2 ${CLIENT_TESTNAME} &

export TOTAL_CLIENT=8
export TOTAL_TIME_AVAILABLE=20
start-test 40 $EACH_CLIENT_ISSUE $TOTAL_CLIENT $TOTAL_TIME_AVAILABLE ${TESTNAME}-part3 ${CLIENT_TESTNAME} &

export TOTAL_CLIENT=1
export TOTAL_TIME_AVAILABLE=40
start-test 80 $EACH_CLIENT_ISSUE $TOTAL_CLIENT $TOTAL_TIME_AVAILABLE ${TESTNAME}-part4 ${CLIENT_TESTNAME} &

#export TOTAL_CLIENT=15
#export TOTAL_TIME_AVAILABLE=20
#start-test 80 $EACH_CLIENT_ISSUE $TOTAL_CLIENT $TOTAL_TIME_AVAILABLE ${TESTNAME}-part5 ${CLIENT_TESTNAME} &
#
#export TOTAL_CLIENT=16
#export TOTAL_TIME_AVAILABLE=10
#start-test 90 $EACH_CLIENT_ISSUE $TOTAL_CLIENT $TOTAL_TIME_AVAILABLE ${TESTNAME}-part6 ${CLIENT_TESTNAME} &

wait;

mkdir -p $TESTNAME-result;

for h in "${hosts[@]}"; do
    scp "$h:/tmp/$h-$TESTNAME*" $TESTNAME-result/
done
cp start-proxy-args.sh run-scala-test.sh $TESTNAME-result/
scp proxy-1:/tmp/proxy-report.json $TESTNAME-result/proxy-report-1.json
scp proxy-2:/tmp/proxy-report.json $TESTNAME-result/proxy-report-2.json
scp proxy-3:/tmp/proxy-report.json $TESTNAME-result/proxy-report-3.json
wait < <(jobs -p);

cd "$TESTNAME-result/"
rm -f ${TESTNAME}_summary.csv ${TESTNAME}_summary_for_upload.csv;
python3 ../csv-merge.py ${TESTNAME}_summary.csv *${TESTNAME}*.csv
head -n 1200 ${TESTNAME}_summary.csv > ${TESTNAME}_summary_for_upload.csv
../upload.sh $UPLOAD_GDRIVE $TESTNAME ${TESTNAME}_summary_for_upload.csv
echo -e "\a finish test: $TESTNAME"
