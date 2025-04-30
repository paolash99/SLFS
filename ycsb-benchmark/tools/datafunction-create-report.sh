DIR=$1

cd $DIR;

cat */*.txt | grep throughput | awk '{ print $2 }'
