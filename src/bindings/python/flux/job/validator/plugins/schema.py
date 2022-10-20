##############################################################
# Copyright 2021 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

"""Validate jobspec using jsonschema

Validates jobspec using jsonschema.validate() against a schema
specified via the ``--schema=SCHEMA`` option.

"""

import json
import sys

import jsonschema
from flux.job.validator import ValidatorPlugin


class Validator(ValidatorPlugin):
    def __init__(self, parser):
        parser.add_argument(
            "--schema", type=str, help="Validate with SCHEMA", required=True
        )

    def configure(self, args):
        try:
            with open(args.schema) as fd:
                self.schema = json.load(fd)
        except (OSError, IOError) as e:
            sys.exit(f"{args.schema}: {e.strerror}")
        except ValueError as e:
            sys.exit(f"{args.schema}: {e}")

    def validate(self, args):
        try:
            jsonschema.validate(args.jobspec, self.schema)
        except ValueError as e:
            return (1, str(e))
        except jsonschema.exceptions.ValidationError as e:
            return (1, e.message.replace("u'", "'"))
        return (0, None)
