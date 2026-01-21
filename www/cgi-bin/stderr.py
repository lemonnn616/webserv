#!/usr/bin/python3
import sys
sys.stderr.write("oops on stderr\n")
sys.stdout.write("Content-Type: text/plain\r\n\r\n")
sys.stdout.write("ok\n")
