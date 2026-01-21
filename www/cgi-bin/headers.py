#!/usr/bin/python3
import sys
sys.stdout.write("Status: 200 OK\r\n")
sys.stdout.write("Content-Type: text/plain\r\n")
sys.stdout.write("X-Test: one\r\n")
sys.stdout.write("X-Test: two\r\n")
sys.stdout.write("\r\n")
sys.stdout.write("ok\n")
