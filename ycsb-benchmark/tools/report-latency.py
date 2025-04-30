import csv
import statistics

# Open the CSV file in read mode
with open('report.csv', mode='r', newline='\n') as file:
    csv_reader = csv.DictReader(file)

    latency = []

    simtime = 0
    prev = 0
    for row in csv_reader:
        latency.append(int(row["duration_us"]))
        if prev != 0:
            now = int(row["Timestamp"])
            if (now - prev > 10 * 1000000000):
                simtime += 10 * 1000000000
            else:
                simtime += int(row["duration_us"]) * 1000

        prev = int(row["Timestamp"])
    print(statistics.mean(latency))
    print(statistics.median(latency))
    print(simtime)
