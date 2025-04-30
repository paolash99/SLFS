#!/bin/bash
source start-proxy-args.sh;

TESTNAME="ceph-cache-nocreat${BACKEND_CONFIG_NAME}_T+${CLIENT_TESTNAME}_H+${#hosts[@]}_TH+${TOTAL_CLIENT}"
echo "testname: $TESTNAME"

bash -c 'cd ../proxy; make from-docker; ./transfer_images.sh';

rm -f /tmp/ceph-zipf;
docker run --rm --entrypoint cat hare1039/transport:0.0.2 /bin/ceph-zipf > /tmp/ceph-zipf;

chmod +x /tmp/ceph-zipf

for h in "${hosts[@]}"; do
    ssh "$h" rm ceph-zipf;
    scp /tmp/ceph-zipf "$h": &
done
wait < <(jobs -p);

echo starting;

for h in "${hosts[@]}"; do
    ssh "$h" "rm -f /tmp/ceph-$h-$TESTNAME*";
    ssh "$h" "bash -c '/home/ubuntu/ceph-zipf --basename /mnt/mycephfs/ycsb/ --total-times ${EACH_CLIENT_ISSUE} --total-clients ${TOTAL_CLIENT} --bufsize $BUFSIZE --zipf-alpha 1.05 ${UNIFORM_DIST} --result /tmp/ceph-$h-$TESTNAME --test-name $CLIENT_TESTNAME'" &
done
wait < <(jobs -p);

mkdir -p ceph-$TESTNAME-result;

for h in "${hosts[@]}"; do
    scp "$h:/tmp/ceph-$h-$TESTNAME*" ceph-$TESTNAME-result/
done

cp start-proxy-args.sh ceph-$TESTNAME-result/
scp proxy-1:/tmp/proxy-report.json ceph-$TESTNAME-result/proxy-report-1.json
scp proxy-2:/tmp/proxy-report.json ceph-$TESTNAME-result/proxy-report-2.json
scp proxy-3:/tmp/proxy-report.json ceph-$TESTNAME-result/proxy-report-3.json
wait < <(jobs -p);

cd "ceph-$TESTNAME-result/"
rm -f ${TESTNAME}_summary.csv ${TESTNAME}_summary_for_upload.csv;
python3 ../csv-merge.py ${TESTNAME}_summary.csv *${TESTNAME}*.csv
head -n 1200 ${TESTNAME}_summary.csv > ${TESTNAME}_summary_for_upload.csv
../upload.sh $UPLOAD_GDRIVE ceph-$TESTNAME ${TESTNAME}_summary_for_upload.csv
echo -e "\a finish test: ceph-$TESTNAME"
