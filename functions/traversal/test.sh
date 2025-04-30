#!/bin/bash

#echo "start meta write  /"
#wsk -i action invoke --blocking -v slsfs-metadatafunction \
#    --param type metadata \
#    --param operation addnewfile \
#    --param filename / \
#    --param data hellowld.txt \
#    --param returnchannel "31kijg=="

echo "starting test..."
time wsk -i action invoke --blocking slsfs-datafunction --param operation read --param filename /helloworld.txt --param type file --param position 0 --param size 3;
#time wsk -i action invoke --blocking slsfs-datafunction --param type storagetest
#time wsk -i action invoke --blocking slsfs-datafunction --param operation write --param filename /helloworld.txt --param type file --param data osdiatc2022

python3 ../../pyclient/client.py
