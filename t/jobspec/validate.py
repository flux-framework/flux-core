#!/usr/bin/env python

# Usage: validate.py --schema=jobspec.json data.json [data.json ...]
# Usage: cat data.json | validate.py --schema=jobspec.json

import sys
import argparse
import json
import yaml
import jsonschema


def validate_input(jobspec_stream, schema, label=""):
    errors = 0
    try:
        data = yaml.safe_load(jobspec_stream)
        jsonschema.validate(data, schema)
    except (yaml.YAMLError) as e:
        print("{}: {}".format(label, e.problem))
        errors = errors + 1
    except (Exception) as e:
        print("{}: {}".format(label, e.message))
        errors = errors + 1
    except:
        print("{}: unknown error".format(label))
        errors = errors + 1
    else:
        print("{}: ok".format(label))
    return errors


def validate_inputfile(infile, schema):
    errors = 0
    try:
        with open(infile) as fd:
            errors += validate_input(fd, schema, label=infile)
    except (OSError, IOError) as e:
        print("{}: {}".format(infile, e.strerror))
        errors = errors + 1
    return errors


# parse command line args
parser = argparse.ArgumentParser()
parser.add_argument("--schema", "-s", type=str, required=True)
parser.add_argument("jobspecs", nargs="*")
args = parser.parse_args()

# Parse json-schema file (JSON format)
try:
    schema = json.load(open(args.schema))
except (OSError, IOError) as e:
    sys.exit("{}: {}".format(args.schema, e.strerror))
except:
    sys.exit("{}: unknown error".format(args.schema))

# Validate each file on command line
errors = 0
if args.jobspecs:
    for infile in args.jobspecs:
        errors += validate_inputfile(infile, schema)
else:
    errors += validate_input(sys.stdin, schema, label="stdin")

sys.exit(errors)
