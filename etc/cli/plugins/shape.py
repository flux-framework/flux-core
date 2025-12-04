##############################################################
# Copyright 2025 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import json

from flux.cli.plugin import CLIPlugin
from flux.shape.parser import ShapeParser


class ShapePlugin(CLIPlugin):
    """Wrap the functionality provided by the command-line job shape
    parser into a command-line plugin users can optionally prepend to
    their `FLUX_CLI_PLUGINPATH`. As a convenience for users, also
    provide options for specifying json files on the command-line
    that will get absorbed into the submitted jobspec.
    """

    def __init__(self, prog, prefix="resources"):
        super().__init__(prog, prefix=prefix)
        self.add_option(
            "--shape",
            metavar="SHAPE",
            help="Provide an RFC 46 jobspec shape on the command line. Any other resource arguments will be ignored.",
        )
        self.add_option(
            "--json",
            metavar="FILE",
            help="Provide a JSON file specifying the `resources` section of a jobspec.",
        )

    def preinit(self, args):
        try:
            if args.json or args.shape:
                args.nodes = 1  ## set a number of slots, will be subsequently ignored
        except AttributeError:
            pass

    def modify_jobspec(self, args, jobspec):
        try:
            if args.shape and args.json:
                raise ValueError(
                    "`--resources-shape` and `--resources-json` are mutually exclusive arguments. Use only one."
                )
            if args.shape:
                jobspec.jobspec["resources"] = ShapeParser().parse(args.shape)
            elif args.json:
                with open(args.json, "r") as json_file:
                    data = json.loads(json_file.read())
                    try:
                        jobspec.jobspec["resources"] = data["resources"]
                    except TypeError:
                        jobspec.jobspec["resources"] = data
        except AttributeError:
            pass
