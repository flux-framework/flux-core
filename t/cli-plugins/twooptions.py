##############################################################
# Copyright 2025 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

from flux.cli.plugin import CLIPlugin


class FluxionPlugin(CLIPlugin):
    """Accept command-line option that updates fluxion config in subinstance"""

    def __init__(self, prog):
        super().__init__(prog)
        self.add_option(
            "--match-policy",
            metavar="POLICY",
            help="Set Fluxion's match policy for a subinstance. See sched-fluxion-resource(5) for available policies.",
        )
        self.add_option(
            "--feasibility",
            action="store_true",
            help="Enable the feasibility validator plugin for a subinstance.",
        )

    def preinit(self, args):
        pol = args.match_policy
        if self.prog in ("batch", "alloc"):
            if pol:
                args.conf.update(f'sched-fluxion-resource.match-policy="{pol}"')
            if args.feasibility:
                args.conf.update("ingest.validator.plugins=['jobspec', 'feasibility']")

    def modify_jobspec(self, args, jobspec):
        jobspec.setattr("system.fluxion_match_policy", str(args.match_policy))

    def validate(self, jobspec):
        try:
            pol = jobspec.attributes["system"]["fluxion_match_policy"]
        except KeyError:
            pol = "None"
        if pol != "firstnodex" and pol != "None":
            raise ValueError(f"Invalid option for fluxion-match-policy: {pol}")
