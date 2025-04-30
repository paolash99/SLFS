
source start-proxy-args.sh;

EACH_CLIENT_ISSUE=1000

for h in "${hosts[@]}"; do
    ssh "$h" "bash -c '/home/ubuntu/slsfs-client --total-times ${EACH_CLIENT_ISSUE} --total-clients ${TOTAL_CLIENT} --bufsize 4096 --uniform-dist --result /tmp/1 --test-name 100-0'" &
done
wait < <(jobs -p);
