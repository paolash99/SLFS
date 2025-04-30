

START=$(sed '2q;d' report.csv | cut -d',' -f1);
END=$(tail -1 report.csv | cut -d',' -f1);

echo $(( $(( $END - $START )) / 1000000000 ));
