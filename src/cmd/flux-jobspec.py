import re
import os
import sys
import math
import logging
import argparse
import json
import collections.abc as abc

import yaml

from flux import util


def create_resource(res_type, count, with_child=None):
    if with_child is None:
        with_child = []
    else:
        assert isinstance(with_child, abc.Sequence), "child resource must be a sequence"
        assert not isinstance(with_child, str), "child resource must not be a string"

    assert count > 0, "resource count must be > 0"

    res = {"type": res_type, "count": count}

    if with_child:  # if not empty
        res["with"] = with_child
    return res


def create_slot(label, count, with_child):
    slot = create_resource("slot", count, with_child)
    slot["label"] = label
    return slot


def get_environment(export_list=None):
    """
    Return current environment as specified by argument `export_list`, a list of
    dicts possibly provided by the --export command line option.  If "ALL" is
    set always export all environment variables, else if "NONE" is set, export
    an empty environment.
    """
    if export_list is None:
        export_list = [{}]

    environ = {}

    # Argument from --export is a list of dicts. Combine this list into
    #  a single "exports" dict:
    exports = {k: v for e in export_list for (k, v) in e.items()}

    # If --export option not used then return current environment:
    if not exports:
        return dict(os.environ)

    # If --export=NONE then return empty environment:
    if "NONE" in exports:
        return {}

    # If --export=ALL,... then start with current environment, possibly
    # modified by export --export arguments:
    if "ALL" in exports:
        del exports["ALL"]
        environ = dict(os.environ)

    # Set each env var to provided value, e.g. --export=FOO=bar,
    #  or current value if not provided, e.g. --export=FOO :
    for key, value in exports.items():
        try:
            environ[key] = value or os.environ[key]
        except KeyError:
            LOGGER.error("Variable %s not found in current env", str(key))
            sys.exit(1)
    return environ


def create_slurm_style_jobspec(
    command, num_tasks, cores_per_task, num_nodes=0, walltime=None, environ=None
):
    if environ is None:
        environ = get_environment()

    core = create_resource("core", cores_per_task)
    if num_nodes > 0:
        num_slots = int(math.ceil(num_tasks / float(num_nodes)))
        if num_tasks % num_nodes != 0:
            logging.warning(
                "Number of tasks is not an integer multiple of the number of nodes. "
                "More resources than required will be requested to satisfy your job."
            )
            task_count_dict = {"total": num_tasks}
        else:
            task_count_dict = {"per_slot": 1}
        slot = create_slot("task", num_slots, [core])
        resource_section = create_resource("node", num_nodes, [slot])
    else:
        task_count_dict = {"per_slot": 1}
        slot = create_slot("task", num_tasks, [core])
        resource_section = slot

    jobspec = {
        "version": 1,
        "resources": [resource_section],
        "tasks": [{"command": command, "slot": "task", "count": task_count_dict}],
        "attributes": {"system": {"cwd": os.getcwd(), "environment": environ}},
    }
    if not walltime:
        walltime = 0.0

    jobspec["attributes"]["system"]["duration"] = walltime

    return jobspec


def validate_slurm_args(args):
    if args.nodes > args.ntasks:
        raise ValueError("Number of nodes greater than the number of tasks")

    if (
        args.time
        and re.match(r"^(\d+-)?\d+:\d+:\d+$", args.time) is None
        and re.match(r"^\d+(:\d+)?$", args.time) is None
    ):
        raise ValueError(
            "invalid time limit string format. "
            "Acceptable formats include minutes[:seconds], [days-]hours:minutes:seconds"
        )

    # TODO: is there any validation of the stdout redirection path that we can do?
    # IDEA: print a warning if the file already exists or the parent dir doesn't exist


#  Convert slurm walltime string to floating point seconds
def slurm_walltime_to_duration(time_str):
    if not time_str:
        return None
    dhms_regex = re.compile(
        r"^((?P<days>\d+)-)?"
        r"(?P<hours>\d+):"
        r"(?P<minutes>\d+):"
        r"(?P<seconds>\d+)$"
    )
    ms_regex = re.compile(r"^(?P<minutes>\d+)(:(?P<seconds>\d+))?$")

    time = 0.0
    match = ms_regex.search(time_str) or dhms_regex.search(time_str)
    if match is None:
        return None
    vals = {k: float(v) for k, v in match.groupdict().items() if v is not None}
    if "days" in vals:
        time = time + vals["days"] * 60 * 60 * 24
    if "hours" in vals:
        time = time + vals["hours"] * 60 * 60
    if "minutes" in vals:
        time = time + vals["minutes"] * 60
    if "seconds" in vals:
        time = time + vals["seconds"]
    if time == 0.0:
        return None
    return time


def slurm_jobspec(args):
    if args.ntasks is None:
        args.ntasks = max(args.nodes, 1)

    try:
        validate_slurm_args(args)
    except ValueError as err:
        LOGGER.error(str(err))
        sys.exit(1)
    time = slurm_walltime_to_duration(args.time)
    environ = get_environment(args.export)
    return create_slurm_style_jobspec(
        args.command, args.ntasks, args.cpus_per_task, args.nodes, time, environ
    )


def kv_list_split(string):
    """
    Split a key/value list with optional values, e.g. 'FOO,BAR=baz,...'
    and return a dict.
    """
    return dict((a, b) for a, _, b in [x.partition("=") for x in string.split(",")])


def get_slurm_common_parser():
    """
    Shared arguments amongst srun and sbatch.
    Used src/srun/libsrun/opt.c and src/sbatch/opt.c of the
    [SLURM repository](https://github.com/SchedMD/slurm.git) as reference
    """
    slurm_parser = argparse.ArgumentParser(add_help=False)
    slurm_parser.add_argument(
        "-N",
        "--nodes",
        help="Set the number of requested nodes to N",
        type=int,
        metavar="N",
        default=0,
    )
    slurm_parser.add_argument(
        "-n", "--ntasks", help="Set the number of tasks to N", type=int, metavar="N"
    )
    slurm_parser.add_argument(
        "-c",
        "--cpus-per-task",
        help="Set number of cores per task to N",
        type=int,
        metavar="N",
        default=1,
    )
    slurm_parser.add_argument(
        "-t",
        "--time",
        help="time limit. Acceptable formats include minutes[:seconds], "
        "[days-]hours:minutes:seconds",
    )
    slurm_parser.add_argument("-o", "--output", help="location of stdout redirection")
    slurm_parser.add_argument(
        "--export",
        metavar="[ALL|NONE|VARS]",
        action="append",
        type=kv_list_split,
        default=[{}],
        help="List or specify environment variables to export",
    )
    slurm_parser.add_argument("command", nargs=argparse.REMAINDER)
    return slurm_parser


LOGGER = logging.getLogger("flux-jobspec")


@util.CLIMain(LOGGER)
def main():
    parser = argparse.ArgumentParser(prog="flux-jobspec")
    parser.add_argument("--format", choices=["json", "yaml"], default="json")

    subparsers = parser.add_subparsers()
    slurm_parser = get_slurm_common_parser()
    srun_parser = subparsers.add_parser(
        "srun",
        parents=[slurm_parser],
        help="subcommand for SLURM-style CLI arguments",
        formatter_class=util.help_formatter(),
    )
    srun_parser.set_defaults(func=slurm_jobspec)

    args = parser.parse_args()

    if not args.command:  # list is empty
        parser.error("command is required")
        sys.exit(1)

    jobspec = args.func(args)

    if args.format == "yaml":
        out = yaml.dump(jobspec)
    else:
        out = json.dumps(jobspec, ensure_ascii=False)
    print(out)


if __name__ == "__main__":
    main()
