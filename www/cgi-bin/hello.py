#!/usr/bin/env python3
import os
import time
import urllib.parse

qs = os.environ.get("QUERY_STRING", "")
params = urllib.parse.parse_qs(qs)

method = os.environ.get("REQUEST_METHOD", "")
script = os.environ.get("SCRIPT_NAME", "")
path_info = os.environ.get("PATH_INFO", "")
remote = os.environ.get("REMOTE_ADDR", "")
pid = os.getpid()

name = params.get("name", ["Anonymous"])[0]
now = time.strftime("%Y-%m-%d %H:%M:%S")

body = f"""<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <title>CGI Hello</title>
  <style>
    body {{ font-family: Arial; background:#0b1220; color:#e5e7eb; padding:20px; }}
    .card {{ background:#111827; border:1px solid #1f2937; border-radius:8px; padding:16px; max-width:700px; }}
    code {{ color:#93c5fd; }}
  </style>
</head>
<body>
  <div class="card">
    <h1>✅ CGI is running</h1>
    <p>Hello, <b>{name}</b>!</p>
    <p><b>Time:</b> <code>{now}</code></p>
    <p><b>PID:</b> <code>{pid}</code></p>
    <p><b>Method:</b> <code>{method}</code></p>
    <p><b>Script:</b> <code>{script}</code></p>
    <p><b>Path info:</b> <code>{path_info}</code></p>
    <p><b>Query:</b> <code>{qs}</code></p>
    <p><b>Client:</b> <code>{remote}</code></p>
  </div>
</body>
</html>
"""

print("Status: 200 OK")
print("Content-Type: text/html; charset=utf-8")
print("Content-Length: " + str(len(body.encode("utf-8"))))
print()
print(body)
