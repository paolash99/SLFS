
ID="$1"
testname="$2";
datafile="$3";


tbx services google sheets sheet create \
    -id $ID \
    -title "$testname"

tbx services google sheets sheet import \
    -id $ID \
    -data "$datafile" \
    -range "$testname!A1:ZZZ100000"
