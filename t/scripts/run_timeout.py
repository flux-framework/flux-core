#!/usr/bin/env python3
"""run command with a timeout, timeout as a float is first argument, rest are command"""
import sys
import os
import signal
import argparse
import subprocess as s

parser = argparse.ArgumentParser(description="run command with a timeout")
parser.add_argument(
    "-s", "--signal", help="signal to send, default is SIGKILL", default=signal.SIGKILL
)
parser.add_argument(
    "-e",
    "--env",
    help="environment variable to set of the form KEY=VAL",
    action="append",
    default=[],
)
parser.add_argument(
    "-k",
    "--kill-after",
    type=float,
    help="secondary timeout before kill if first does not kill it",
    default=1.0,
)
parser.add_argument("timeout", type=float, help="timeout in float seconds")
parser.add_argument("cmd")
parser.add_argument("cmd_args", nargs=argparse.REMAINDER)

args = parser.parse_args()


def resolve_signal():
    if not isinstance(args.signal, int):
        try:
            args.signal = int(args.signal)
            return
        except ValueError:
            pass
        try:
            args.signal = getattr(signal, args.signal)
            return
        except AttributeError:
            pass
        try:
            args.signal = getattr(signal, f"SIG{args.signal}")
            return
        except AttributeError:
            pass
        raise ValueError(f"value passed for signal is invalid: {args.signal}")


def exit_signal(rc):
    # python "helpfully" translates signal return codes for us, translate back
    if rc < 0:
        rc = 128 + abs(rc)
    sys.exit(rc)


def do_timeout():
    environ = dict(os.environ)
    for e in args.env:
        (k, v) = e.split("=")
        environ[k] = v
    try:
        # add cmd onto the front of the cmd arg list
        args.cmd_args.insert(0, args.cmd)
        p = s.Popen(args.cmd_args, env=environ)
        # run with timeout, on success exits with return code
        r = p.wait(timeout=args.timeout)
    except s.TimeoutExpired:
        # send signal to timeout process
        p.send_signal(args.signal)
        if args.kill_after > 0:
            try:
                # wait to make sure it actually stops
                r = p.wait(timeout=args.kill_after)
            except s.TimeoutExpired:
                p.kill()
                r = p.wait()
    exit_signal(r)


resolve_signal()
do_timeout()
