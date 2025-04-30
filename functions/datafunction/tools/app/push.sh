#!/bin/bash
tag=0.0.$(shuf -i 1-100000000 -n 1);
set -e;

docker build -t hare1039/example-app-slsfs:$tag -f Dockerfile ..
docker push hare1039/example-app-slsfs:$tag
wsk -i action delete example-app-slsfs || true;
wsk -i action create example-app-slsfs --docker hare1039/example-app-slsfs:$tag --concurrency 500

if [[ "$BUILDONLY" == "TRUE" ]]; then
    exit 0;
fi

#echo "start create /"
#wsk -i action invoke --blocking slsfs-datafunction --param operation create --param filename / --param type metadata

#echo "start create /hello.txt"
#wsk -i action invoke --blocking slsfs-datafunction --param operation create --param filename /hello.txt --param type metadata

wsk -i action invoke --blocking example-app-slsfs --param data lemonade
#    wsk -i action invoke --blocking cpp --param filename / --param operation ls --result && \
#    wsk -i action invoke --blocking cpp --param filename /directoy --param operation ls --result && \
#    wsk -i action invoke --blocking cpp --param filename /directoy/cccc.txt --param operation touch --result && \
#    wsk -i action invoke --blocking cpp --param filename /directoy --param operation ls --result && \
#    wsk -i action invoke --blocking cpp --param filename /directoy/cccc.txt --param operation write --param data goosemonitor --result && \
#    wsk -i action invoke --blocking cpp --param filename /directoy/cccc.txt --param operation read --result
