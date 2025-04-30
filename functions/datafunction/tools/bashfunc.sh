#!/bin/bash

while read line
do
    name="$(echo $line | jq -r .value.name)"
    test "$name" == "null" && name="world"
    echo msg="hello $name"
    echo '{"bash": "Hello, '$name'"}' >&3
done
