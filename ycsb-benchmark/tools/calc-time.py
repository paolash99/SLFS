
import sys

import collections
d = collections.defaultdict(lambda: 0)


for l in sys.argv[1:]:
    with open(l) as f:
        for line in f:
            n = line.split()
            time = n[0].split(".")[0] #'[15:59:57.323966'
            iops = int(n[-1])
            d[time] += iops

for i in d.values():
    print(i)
