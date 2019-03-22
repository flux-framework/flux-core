#!/usr/bin/env python

from __future__ import print_function

import sys
import argparse
import json
from collections import Sequence


def create_resource(res_type, count, with_child=[]):
    assert isinstance(with_child, Sequence)
    assert not isinstance(with_child, str)
    assert count > 0

    res = {"type": res_type, "count": count}

    if len(with_child) > 0:
        res["with"] = with_child
    return res


def create_slot(label, count, with_child):
    slot = create_resource("slot", count, with_child)
    slot["label"] = label
    return slot


def main():
    core = create_resource("core", args.cores_per_task)
    slot = create_slot("task", 1, [core])
    if args.num_nodes:
        resource_section = create_resource("node", args.num_nodes, [slot])
    else:
        resource_section = slot

    jobspec = {
        "version": 1,
        "resources": [resource_section],
        "tasks": [
            {
                "command": args.command,
                "slot": "task",
                "count": {"total": args.num_tasks},
            }
        ],
        "attributes": {"system": {"duration": args.walltime}},
    }

    if args.pretty:
        out = json.dumps(jobspec, indent=4, sort_keys=True)
    else:
        out = json.dumps(jobspec)
    print(out)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("-N", "--num-nodes", type=int, default=0)
    parser.add_argument("-n", "--num-tasks", type=int, default=1)
    parser.add_argument("-c", "--cores-per-task", type=int, default=1)
    parser.add_argument(
        "-t", "--walltime", type=int, default=3600, help="walltime in seconds"
    )
    parser.add_argument(
        "-p", "--pretty", action="store_true", help="pretty print the jobspec json"
    )
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()

    if len(args.command) == 0:
        parser.print_usage()
        print("command is required")
        sys.exit(1)

    main()
