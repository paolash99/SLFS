cat *output.txt | grep 'Throughput'

total=0;
for i in $(cat *output.txt | grep 'Throughput' | awk '{ print $5 }'); do
    total=$(awk "BEGIN {print $total + $i}");
done

echo $total
