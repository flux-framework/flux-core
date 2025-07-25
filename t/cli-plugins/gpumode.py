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


class RediscoverGPUPlugin(CLIPlugin):
    """Set the AMD SMI compute partition GPU option.

    More detail on specific compute modes and their respective features is
    available in the ROCm documentation:
    https://rocm.docs.amd.com/projects/amdsmi/en/latest/doxygen/docBin/html/amdsmi_8h.html
    See the ``amdsmi_compute_partition_type_t`` section.
    """

    def __init__(self, prog, prefix="amd"):
        super().__init__(prog, prefix=prefix)
        self.add_option(
            "--gpumode",
            help="Option for setting AMD SMI compute partitioning.",
            choices=["CPX", "TPX", "SPX"],
        )

    def preinit(self, args):
        if self.prog in ("batch", "alloc") and args.gpumode:
            gpumode = args.gpumode
            if gpumode == "TPX" or gpumode == "CPX":
                args.conf.update("resource.rediscover=true")
            elif gpumode == "SPX" or gpumode is None:
                pass
            else:
                raise ValueError("--gpumode can only be set to CPX, TPX, or SPX")

    def modify_jobspec(self, args, jobspec):
        if args.gpumode:
            jobspec.setattr("gpumode", args.gpumode)
