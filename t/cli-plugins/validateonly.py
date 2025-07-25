##############################################################
# Copyright 2025 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

from typing import Mapping

from flux.cli.plugin import CLIPlugin


class ValidateShellVerboseOpt(CLIPlugin):
    name = "shell.options.verbose"

    def validate(self, jobspec):
        try:
            verbosity = jobspec.attributes["system"]["shell"]["options"]["verbose"]
            if not isinstance(verbosity, int):
                raise ValueError(
                    f"{self.name}: expected integer, got {type(verbosity)}"
                )
        except KeyError:
            return


class ValidateShellSignalOpt(CLIPlugin):
    name = "shell.options.signal"

    def validate(self, jobspec):
        try:
            signal = jobspec.attributes["system"]["shell"]["options"]["signal"]
            if isinstance(signal, int):
                return
            if not isinstance(signal, Mapping):
                raise ValueError(
                    f"{self.name}: expected int or mapping got {type(signal)}"
                )
            for name in ("signum", "timeleft"):
                if name in signal:
                    if not isinstance(signal[name], int):
                        typename = type(signal[name])
                        raise ValueError(
                            f"{self.name}.{name}: expected integer, got {typename}"
                        )
            # Check for extra keys:
            extra_keys = set(signal.keys()) - {"signum", "timeleft"}
            if extra_keys:
                raise ValueError(f"{self.name}: unsupported keys: {extra_keys}")
        except KeyError:
            return
