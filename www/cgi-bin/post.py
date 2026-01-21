#!/usr/bin/python3
import os, sys
cl = int(os.environ.get("CONTENT_LENGTH","0") or "0")
body = sys.stdin.read(cl) if cl > 0 else ""
sys.stdout.write("Content-Type: text/plain\r\n\r\n")
sys.stdout.write("len=" + str(len(body)) + "\n")
sys.stdout.write("body=" + body + "\n")
