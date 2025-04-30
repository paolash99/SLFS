

ssh ow-invoker-1 docker restart invoker0 &
ssh ow-invoker-2 docker restart invoker1 &
ssh ow-invoker-3 docker restart invoker2 &

strip exec;
zip -r hello.zip exec
wsk -i action update appv2 hello.zip --docker openwhisk/actionloop-base --concurrency 500;
#wsk -i action update slsfs-metadatafunction hello.zip --docker openwhisk/actionloop-base --concurrency 500;

rm -f hello.zip;

wait
