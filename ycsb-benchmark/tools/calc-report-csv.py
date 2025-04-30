import csv
import statistics

with open("report.csv") as f:
    reader = csv.DictReader(f)

    latency = []
    count = 0
    for row in reader:
        if count == 0:
            starttime = int(row["Timestamp"])
        latency.append(int(row["duration_us"]))
        endtime = int(row["Timestamp"])
        count += 1

    print("latency", statistics.mean(latency))
    print("count", count)
    print("time", (endtime - starttime) / 1000000000.0)
    print("tput", count / ((endtime - starttime) / 1000000000.0))
