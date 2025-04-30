import json
import csv
import statistics

with open("report.csv") as f:
    csv_reader = csv.DictReader(f, delimiter=',')

    latency = []

    for line in csv_reader:
        latency.append(int(line["duration_us"]))

    print ("latency: ", statistics.mean(latency))
