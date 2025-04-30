#!/bin/bash

make from-docker

docker run --rm hare1039/trace_emulator:build cat /bin/trace_emulator > trace_emulator

scp trace_emulator ow-invoker-9:

rm trace_emulator

ssh ow-invoker-9 "chmod +x trace_emulator"

ssh ow-invoker-9

scp ow-invoker-9:report.csv .

ssh ow-invoker-9 "rm report.csv"
ssh ow-invoker-9 "rm trace_emulator"
