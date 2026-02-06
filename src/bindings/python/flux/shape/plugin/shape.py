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


class RFC44ShapePlugin(CLIPlugin):
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
        if getattr(args, "json") or getattr(args, "shape"):
            args.nodes = 1  ## set a number of slots, will be subsequently ignored

    def modify_jobspec(self, args, jobspec):
        s = getattr(args, "shape")
        j = getattr(args, "json")
        if s and j:
            raise ValueError(
                "`--resources-shape` and `--resources-json` are mutually exclusive arguments. Use only one."
            )
        if s:
            jobspec.jobspec["resources"] = ShapeParser().parse(s)
        elif j:
            with open(j, "r") as json_file:
                data = json.loads(json_file.read())
                try:
                    jobspec.jobspec["resources"] = data["resources"]
                except AttributeError:
                    jobspec.jobspec["resources"] = data
        else:
            pass
