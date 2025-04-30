#import matplotlib.pyplot as plt
import json

with open("proxy-report-1.json") as fp:
    data = json.load(fp)

history = data["history"]

history = sorted(history, key=lambda x: x["timestamp"])
for h in history:
    print (h["timestamp"])#,
    #print (h["worker_count"])
    #print (h["number_of_incoming_request"])
print("v")

for h in history:
    #print (h["timestamp"])#,
    print (h["worker_count"])
    #print (h["number_of_incoming_request"])

print("v")
for h in history:
    #print (h["timestamp"])#,
    #print (h["worker_count"])
    print (h["number_of_incoming_request"])
#
#x = [x["timestamp"] for x in history]
#df_count = [x["worker_count"] for x in history]
#request = [x["number_of_incoming_request"] for x in history]
#
#fig, ax = plt.subplots() #2, 2, sharex=True, sharey=True
##ax.set_xlabel()
##ax.set_xlim()
#ax2 = ax.twinx()
#ax.plot(x, df_count, label="Data function", color="blue")
#ax2.plot(x, request, label="Requests", color="red")
#fig.suptitle("Requests")
#fig.legend()
#plt.tight_layout()
#plt.show()
##plt.clf()
