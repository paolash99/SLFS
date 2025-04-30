import csv
import sys
import statistics
import collections
import json

def good_mean(arr):
    try:
        return statistics.mean(arr)
    except:
        return 0

if __name__ == "__main__":
    df_duration = []
    try:
        with open("proxy-report-1.json") as fp:
            proxy_report1 = json.load(fp)
            for df in proxy_report1["df"]:
                df_duration.append(df["duration"])

    except:
        pass

    try:
        with open("proxy-report-2.json") as fp:
            proxy_report2 = json.load(fp)
            for df in proxy_report2["df"]:
                df_duration.append(df["duration"])
    except:
        pass

    try:
        with open("proxy-report-3.json") as fp:
            proxy_report3 = json.load(fp)
            for df in proxy_report3["df"]:
                df_duration.append(df["duration"])
    except:
        pass

    print(sum(df_duration) / 1000000000)
