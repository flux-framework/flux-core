#!/usr/bin/env python3

import argparse
import json
from types import SimpleNamespace
from unittest.mock import patch

from flux.job import JobspecV1

from flux.cli.plugins import cosched


class FakeFlux:
    """Minimal replacement for flux.Flux used by the plugin."""

    def __init__(self, config):
        self.config = config

    def conf_get(self, key, default=None):
        return self.config.get(key, default)


def parse_args():
    parser = argparse.ArgumentParser(
        description="Test the cosched jobspec transformation."
    )

    parser.add_argument(
        "--ntasks",
        type=int,
        default=6,
        help="Number of tasks in the input jobspec (default: 6)",
    )

    parser.add_argument(
        "--allowed",
        dest="allowed",
        action="store_true",
        help="Enable co-scheduling",
    )

    parser.add_argument(
        "--no-allowed",
        dest="allowed",
        action="store_false",
        help="Disable co-scheduling",
    )

    parser.add_argument(
        "--resource-type",
        default="numanode",
        help="Parent resource type used for co-scheduling (default: numanode)",
    )

    parser.add_argument(
        "--waste-threshold",
        type=float,
        default=1.0,
        help="Maximum allowed waste ratio (default: 1.0)",
    )

    parser.add_argument(
        "--n-way",
        type=int,
        default=2,
        help="Number of jobs sharing each resource group (default: 2)",
    )

    parser.add_argument(
        "--cores-per-resource",
        type=int,
        default=10,
        help="Mocked number of cores per parent resource (default: 10)",
    )

    parser.add_argument(
        "--command",
        nargs=argparse.REMAINDER,
        default=["app"],
        help="Command stored in the jobspec",
    )

    parser.set_defaults(allowed=None)

    return parser.parse_args()


def main():
    args = parse_args()

    config = {
        "cosched.resource_type": args.resource_type,
        "cosched.waste_threshold": args.waste_threshold,
        "cosched.n_way": args.n_way,
    }

    if args.allowed is not None:
        config["cosched.allowed"] = args.allowed

    jobspec = JobspecV1.from_command(
        args.command,
        num_tasks=args.ntasks,
    )

    plugin = object.__new__(cosched.CoSchedPlugin)

    fake_flux_factory = lambda: FakeFlux(config)

    with patch.object(cosched, "Flux", fake_flux_factory):
        with patch.object(
            plugin,
            "find_cores_per_resource",
            return_value=args.cores_per_resource,
        ):
            plugin.modify_jobspec(
                args=SimpleNamespace(),
                jobspec=jobspec,
            )

    output = dict(jobspec.jobspec)
    output.pop("attributes", None)
    print(json.dumps(output, indent=2))


if __name__ == "__main__":
    main()
