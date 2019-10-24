# Usage: flux python request-and-unload.py module-name

import argparse
import sys
import errno
import flux
import json


def expect_enosys(rpc, timeout=1):
    try:
        rpc.wait_for(timeout=timeout)
        rpc.get()
    except EnvironmentError as e:
        if e.errno == errno.ENOSYS:
            print("Successfully received ENOSYS")
            return
        elif e.errno == errno.ETIMEDOUT:
            sys.exit("Request timed out")
        else:
            sys.exit("Unexpected errno: {}".format(e))
    raise RuntimeError("Did not receive ENOSYS")


def main():
    h = flux.Flux()

    alloc = h.rpc("sched.alloc", json.dumps({"id": 0}))
    free = h.rpc("sched.free", json.dumps({"id": 0}))
    print("Sent alloc and free requests")

    h.rpc("cmb.rmmod", json.dumps({"name": args.sched_module})).get()
    print("Removed {}".format(args.sched_module))

    expect_enosys(alloc)
    expect_enosys(free)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("sched_module")
    args = parser.parse_args()

    main()
