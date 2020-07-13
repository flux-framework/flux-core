##############################################################
#  Copyright 2018 Lawrence Livermore National Security, LLC
#  (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
#  This file is part of the Flux resource manager framework.
#  For details, see https://github.com/flux-framework.
#
#  SPDX-License-Identifier: LGPL-3.0
##############################################################

from __future__ import print_function

import sys
import json
import argparse

from flux.job import validate_jobspec


def emit(object_dict):
    s = json.dumps(object_dict, separators=(",", ":"))
    print(s)
    sys.stdout.flush()


print("ready", file=sys.stderr)

parser = argparse.ArgumentParser()
parser.add_argument("--require-version", type=int)
args = parser.parse_args()

if args.require_version is not None:
    if args.require_version < 1:
        print(
            "Required version too low: {} is < 1".format(args.require_version),
            file=sys.stderr,
        )
        exit(1)
    elif args.require_version > 2:
        print(
            "Required version too high: {} is > 2".format(args.require_version),
            file=sys.stderr,
        )
        exit(1)

while True:
    line = sys.stdin.readline()
    if line == "":
        break
    errnum, errstr = (0, None)
    try:
        validate_jobspec(line, args.require_version)
    except (ValueError, TypeError, EnvironmentError) as e:
        errnum, errstr = (1, str(e))
    if errstr != None:
        emit({"errnum": errnum, "errstr": errstr})
    else:
        emit({"errnum": errnum})

print("exiting", file=sys.stderr)
