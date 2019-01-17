#!/usr/bin/env python

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
import argparse
import json
import jsonschema


def emit(object_dict):
    s = json.dumps(object_dict, separators=(",", ":"))
    print(s)
    sys.stdout.flush()


def validate(schema, line):
    try:
        jobspec = json.loads(line)
        jsonschema.validate(jobspec, schema)
    except ValueError as e:
        return (1, str(e))
    except jsonschema.exceptions.ValidationError as e:
        return (1, e.message.replace("u'", "'"))
    return (0, None)


parser = argparse.ArgumentParser()
parser.add_argument("--schema", "-s", type=str, required=True)
args = parser.parse_args()

try:
    with open(args.schema) as fd:
        schema = json.load(fd)
except (OSError, IOError) as e:
    sys.exit(args.schema + ": " + e.strerror)
except ValueError as e:
    sys.exit(args.schema + ": " + str(e))

print("ready", file=sys.stderr)

while True:
    line = sys.stdin.readline()
    if line == "":
        break
    errnum, errstr = validate(schema, line)
    if errstr != None:
        emit({"errnum": errnum, "errstr": errstr})
    else:
        emit({"errnum": errnum})

print("exiting", file=sys.stderr)
