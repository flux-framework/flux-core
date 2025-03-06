from typing import Mapping

from flux.cli.plugin import CLIPlugin


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
