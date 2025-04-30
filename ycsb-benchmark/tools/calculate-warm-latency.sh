echo enter selected version
read version;

for id in $(cat activation-list.txt | grep 0.0.$version | grep 'warm' | awk '{ print $3 }'); do
    wsk -i activation get $id | jq .annotations
    break;
done
