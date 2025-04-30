#!/bin/bash

set -e;

rm -f exec hello.zip;

docker build -t hare1039/appv2:nightly --build-arg CPPBUILDARGS="$CPPBUILDARGS" -f Dockerfile ..

docker run --rm hare1039/appv2:nightly cat /action/exec > exec

chmod +x exec
