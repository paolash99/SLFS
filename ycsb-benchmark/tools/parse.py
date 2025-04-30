import json

f = open('proxy-report-6.json')

data = json.load(f)

for x in data["history"]:
    print (x["finished_job_count"])
