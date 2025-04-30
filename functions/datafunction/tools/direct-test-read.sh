curl "https://192.168.0.224/api/v1/namespaces/_/actions/slsfs-datafunction-0?blocking=true" \
     -X POST \
     -H "Authorization: Basic MjNiYzQ2YjEtNzFmNi00ZWQ1LThjNTQtODE2YWE0ZjhjNTAyOjEyM3pPM3haQ0xyTU42djJCS0sxZFhZRnBYbFBrY2NPRnFtMTJDZEFzTWdSVTRWck5aOWx5R1ZDR3VNREdJd1A=" \
     -H "Content-Type: application/json" \
     -d @ssbd-direct-read.json \
     -k | python3 -m json.tool
