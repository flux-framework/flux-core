#!/usr/bin/env python

from __future__ import print_function

import sys
import json
import argparse

import yaml


def load_jobspec(stream, prefix=""):
    try:
        jobspec = yaml.safe_load(stream)
        return jobspec
    except (yaml.YAMLError) as e:
        print("{}{}".format(prefix, e.problem))
        sys.exit(1)


def summarize_jobspec(jobspec):
    first_resource = jobspec["resources"][0]
    if first_resource["type"] == "node":
        node_entry = first_resource
        slot_entry = node_entry["with"][0]
    else:
        node_entry = None
        slot_entry = first_resource
    core_entry = slot_entry["with"][0]
    task_entry = jobspec["tasks"][0]

    num_nodes = node_entry["count"] if node_entry else 0
    num_slots = slot_entry["count"] * max(num_nodes, 1)
    num_cores = core_entry["count"] * num_slots

    if "total" in task_entry["count"].keys():
        num_tasks = task_entry["count"]["total"]
    elif "per_slot" in task_entry["count"].keys():
        num_tasks = task_entry["count"]["per_slot"] * num_slots
    else:
        raise ValueError("Unknown key in the task's `count` dict")

    return {
        "total_num_nodes": num_nodes,
        "total_num_tasks": num_tasks,
        "total_num_cores": num_cores,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-j", "--jobspec", help="path to file containing the jobspec to query"
    )
    args = parser.parse_args()

    if args.jobspec:
        try:
            with open(args.jobspec, "r") as fd:
                jobspec = yaml.safe_load(fd)
        except (OSError, IOError) as e:
            print("{}{}".format(args.jobspec, e.strerror))
            sys.exit(1)
    else:
        jobspec = load_jobspec(sys.stdin, "stdin: ")

    summary = summarize_jobspec(jobspec)
    print(json.dumps(summary))


if __name__ == "__main__":
    try:
        main()
    except SystemExit as e:  # don't intercept sys.exit calls
        sys.exit(e)
    except Exception as e:
        print("Unknown error: {}".format(e.message), file=sys.stderr)
        sys.exit(1)
    sys.exit(0)
