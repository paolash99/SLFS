
IP=$(ip addr | grep 'inet ' | grep ens3 | awk '{ print $2 }');
IP="${IP%???}"

if [[ -z "$SERVER_ID" ]]; then
    SERVER_ID=-1;
fi

#           -ex=r \
#               --args /bin/slsfs-proxy \
#       --entrypoint gdb -it \


source start-proxy-args.sh;
docker rm -f proxy2;
docker run --privileged -it \
       --name=proxy2 \
       --net=host \
       --volume=/tmp:/tmp \
       hare1039/transport:0.0.2 \
           --listen $PORT \
           --announce $IP \
           $VERBOSE \
           ${NEW_CLUSTER} \
           ${ENABLE_DIRECT_CONNECT} \
           --server-id                "$SERVER_ID" \
           --report                   "/tmp/proxy-report.json" \
           --policy-filetoworker      "$POLICY_FILETOWORKER" \
           --policy-filetoworker-args "$POLICY_FILETOWORKER_ARGS" \
           --policy-launch            "$POLICY_LAUNCH" \
           --policy-launch-args       "$POLICY_LAUNCH_ARGS" \
           --policy-keepalive         "$POLICY_KEEPALIVE" \
           --policy-keepalive-args    "$POLICY_KEEPALIVE_ARGS" \
           --worker-config            "$BACKEND_CONFIG" \
           --max-function-count       "$MAX_FUNCTION_COUNT" \
           --blocksize                "$BACKEND_BLOCKSIZE" \
           ${ENABLE_CACHE} \
           --cache-size               "${CACHE_SIZE}" \
           --cache-policy             "${CACHE_POLICY}"
