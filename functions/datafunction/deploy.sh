

ssh ow-invoker-1  docker restart invoker0 &
ssh ow-invoker-2  docker restart invoker1 &
ssh ow-invoker-3  docker restart invoker2 &
ssh ow-invoker-4  docker restart invoker3 &
ssh ow-invoker-5  docker restart invoker4 &
ssh ow-invoker-6  docker restart invoker5 &
ssh ow-invoker-7  docker restart invoker6 &
ssh ow-invoker-8  docker restart invoker7 &
ssh ow-invoker-9  docker restart invoker8 &
ssh ow-invoker-10 docker restart invoker9 &
ssh ow-invoker-11 docker restart invoker10 &
ssh ow-invoker-12 docker restart invoker11 &
ssh ow-invoker-13 docker restart invoker12 &
ssh ow-invoker-14 docker restart invoker13 &
ssh ow-invoker-15 docker restart invoker14 &
ssh ow-invoker-16 docker restart invoker15 &

strip exec;
zip -r hello.zip exec
wsk -i action update slsfs-datafunction hello.zip --docker openwhisk/actionloop-base --concurrency 500;
wsk -i action update slsfs-metadatafunction hello.zip --docker openwhisk/actionloop-base --concurrency 500;

rm -f hello.zip;

wait
