import sys
print("Python version:", sys.version)
print("Python path:", sys.executable)

import plyvel

db_path = '/home/ubuntu/slsfs/proxy/clients/db1'  # Adjust the path to your actual database location
db = plyvel.DB(db_path, create_if_missing=False)

key_count = 0  # Initialize counter for keys

try:
    for key, value in db:
        key_count += 1  # Increment the key count for each key found
        # print(f'Val: {value} ')
finally:
    db.close()

print("Total number of keys:", key_count)  # Print the total number of keys
