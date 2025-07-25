##############################################################
# Copyright 2025 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import flux
from flux.cli.plugin import CLIPlugin

RC1 = """\
#!/bin/sh
flux exec sh -c 'chmod uo+x $(flux getattr rundir)'
"""


class MultiUserPlugin(CLIPlugin):
    """Add --multi-user option to configure a multi-user subinstance"""

    def __init__(self, prog, prefix=None):
        super().__init__(prog, prefix=prefix)
        if self.prog in ("batch", "alloc"):
            self.add_option(
                "--multi-user",
                action="store_true",
                help="add configuration for multi-user subinstance",
            )

    def preinit(self, args):
        if self.prog in ("batch", "alloc") and args.multi_user:
            imp = flux.Flux().conf_get("exec.imp")
            if imp is None:
                raise ValueError("Can only use --multi-user within multi-user instance")
            args.conf.update("access.allow-guest-user=true")
            args.conf.update("access.allow-root-owner=true")
            args.conf.update(f"exec.imp={imp}")

            # add rc1
            env_arg = "FLUX_RC_EXTRA={{tmpdir}}"
            if args.env:
                args.env.append(env_arg)
            else:
                args.env = [env_arg]

    def modify_jobspec(self, args, jobspec):
        if self.prog in ("batch", "alloc") and args.multi_user:
            jobspec.add_file("rc1.d/rc1", RC1, perms=0o700, encoding="utf-8")
