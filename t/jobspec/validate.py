#!/usr/bin/env python

# Usage: validate.py --schema=jobspec.json data.json [data.json ...]

import sys
import argparse
import json
import yaml
import jsonschema


def validate_input(filename, schema):
    f = open(filename)
    data = yaml.safe_load(f)
    jsonschema.validate(data, schema)


# parse command line args
parser = argparse.ArgumentParser()
parser.add_argument("--schema", "-s", type=str, required=True)
args, unknown = parser.parse_known_args()

# Parse json-schema file (JSON format)
try:
    schema = json.load(open(args.schema))
except (OSError, IOError) as e:
    sys.exit(args.schema + ": " + e.strerror)
except:
    sys.exit(args.schema + ": unknown error")

# Validate each file on command line
errors = 0
for infile in unknown:
    try:
        validate_input(infile, schema)
    except (OSError, IOError) as e:
        print(infile + ": " + e.strerror)
        errors = errors + 1
    except (yaml.YAMLError) as e:
        print(infile + ": " + e.problem)
        errors = errors + 1
    except (Exception) as e:
        print(infile + ": " + e.message)
        errors = errors + 1
    except:
        print(infile + ": unknown error")
        errors = errors + 1
    else:
        print(infile + ": ok")

sys.exit(errors)
