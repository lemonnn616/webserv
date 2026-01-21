#!/usr/bin/python3
import os, sys
sys.stdout.write("Content-Type: text/plain\r\n\r\n")
sys.stdout.write("QUERY_STRING=" + os.environ.get("QUERY_STRING","") + "\n")
sys.stdout.write("REQUEST_METHOD=" + os.environ.get("REQUEST_METHOD","") + "\n")
sys.stdout.write("SCRIPT_NAME=" + os.environ.get("SCRIPT_NAME","") + "\n")
