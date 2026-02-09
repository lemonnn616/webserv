#!/usr/bin/env python3
import time, os

time.sleep(3)

body = f"""<!doctype html>
<html><head><meta charset="utf-8"><title>CGI Sleep</title></head>
<body style="font-family:Arial;background:#0b1220;color:#e5e7eb;padding:20px">
<h1>✅ Slow CGI finished</h1>
<p>slept: <b>3 seconds</b></p>
<p>pid: <code>{os.getpid()}</code></p>
</body></html>
"""

print("Status: 200 OK")
print("Content-Type: text/html; charset=utf-8")
print("Content-Length: " + str(len(body.encode("utf-8"))))
print()
print(body)
