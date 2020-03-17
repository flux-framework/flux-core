#!/usr/bin/env python3
"""run command with a timeout, timeout as a float is first argument, rest are command"""
import sys
import signal
import subprocess as s

if len(sys.argv) < 3:
    print("insufficient arguments passed to run_timeout")
    sys.exit(1)
SIG = signal.SIGKILL
ARG_COUNT = 1
if sys.argv[1] == "-a":
    SIG = signal.SIGALRM
    ARG_COUNT = 2
    if len(sys.argv) < 4:
        print("no command specified")
        sys.exit(1)
TIMEOUT = float(sys.argv[ARG_COUNT])
try:
    P = s.Popen(sys.argv[ARG_COUNT + 1 :])
    sys.exit(P.wait(timeout=TIMEOUT))
except s.TimeoutExpired:
    P.send_signal(SIG)
    try:
        r = P.wait(timeout=0.5)
    except s.TimeoutExpired:
        P.kill()
        r = P.wait()
    sys.exit(r)
