from __future__ import print_function

import re
import os
import sys
import math
import logging
import argparse
import json
import collections.abc as abc
from datetime import timedelta

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
    if walltime:
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


def positive_nonzero_int(string):
    value = int(string)
    if value <= 0:
        msg = "{} is not positive"
        raise argparse.ArgumentTypeError(msg)
    return value


def parse_fsd(string):
    ma = re.match(r".*([smhd])$", string)
    num = float(string[:-1] if ma else string)
    unit = ma.group(1) if ma else "s"

    if unit == "m":
        seconds = timedelta(minutes=num).total_seconds()
    elif unit == "h":
        seconds = timedelta(hours=num).total_seconds()
    elif unit == "d":
        seconds = timedelta(days=num).total_seconds()
    else:
        seconds = num
    return seconds


def resource_walker(res):
    while res is not None and len(res) > 0:
        if type(res) is dict:
            yield res
            res = res.get("with", None)
        elif type(res) is list:
            res = res[0]


def resource_type_walker(res):
    for r in resource_walker(res):
        yield r["type"]


def parse_shape(shape, nslots):
    partlist = shape.split("/")
    if len(partlist) <= 1:
        partlist = shape.split(">")

    count_re = re.compile(
        r"(?P<res>[^\[]+)(?:\[(?P<min>\d+)(?::(?P<max>\d+)(?::(?P<stride>\d+))?)?])?"
    )

    slot_added = False
    # TODO: manual parsing making me grumpy, may pull in pycparser, or use ply
    # if I can stomach it (cffi already depends on it), can't handle unit
    # parsing for counts without doing some more work here
    res = []
    for r in reversed(partlist):
        m = count_re.match(r)
        if len(m.group("res")) < 1:
            raise ValueError("invalid shape, no resource name in {}".format(r))
        if m.group("min") is None:
            count = 1
        else:
            count = int(m.group("min"))
        if m.group("max") is not None:
            count = {
                "min": count,
                "max": int(m.group("max")),
                "operator": "+",
                "operand": 1,
            }
            if m.group("stride") is not None:
                count["operand"] = int(m.group("stride"))
        if m.group("res") == "slot":
            if m.group("min") is not None:
                raise ValueError("slot cannot take count from shape at present")
            slot_added = True
            res = [create_slot("task", nslots, res)]
        else:
            res = [create_resource(m.group("res"), count, res)]
    if slot_added:
        return res
    else:
        return [create_slot("task", nslots, res)]


def flux_jobspec(args):
    # set up defaullts for options where presence matters and check invariants
    if 1 < sum(
        [
            a is not None
            for a in (args.total_tasks, args.tasks_per_slot, args.tasks_per_resource)
        ]
    ):
        LOGGER.error(
            "--total-tasks, --tasks-per-slot and --tasks-per-resource are mutually exclusive"
        )
        sys.exit(1)
    if args.slot_shape is not None and args.shape_file is not None:
        LOGGER.error("--shape and --shape-file are mutually exclusive")
        sys.exit(1)

    if args.nslots is None:
        args.nslots = 1

    if args.tasks_per_slot is None:
        args.tasks_per_slot = 1

    if args.shape_file is not None:
        args.slot_shape = args.slot_shape_file.read().strip()

    if args.slot_shape is None:
        args.slot_shape = "node"

    args.slot_shape = parse_shape(args.slot_shape, args.nslots)

    # TODO: validate shape

    if args.total_tasks is not None:
        task_count_dict = {"total": args.total_tasks}
    elif args.tasks_per_resource is not None:
        task_count_dict = {
            "per_resource": {
                "type": args.tasks_per_resource[0],
                "count": args.tasks_per_resource[1],
            }
        }
        # ensure the type exists in the shape
        if args.tasks_per_resource[0] not in resource_type_walker(args.slot_shape):
            raise ValueError(
                "the resource type named by --tasks-per-resource must be in the requested shape"
            )

    else:
        task_count_dict = {"per_slot": args.tasks_per_slot}

    if args.dir is None:
        args.dir = os.getcwd()

    if args.time is None:
        args.time = parse_fsd("1h")

    if args.env_all and args.env_none:
        LOGGER.error("--env-all and --env-none cannot be combined")
        sys.exit(1)

    environ = {}
    if (not args.env_none) and (args.env_all or args.env is None or len(args.env) == 0):
        environ = dict(os.environ)

    if args.env is not None:
        for e in args.env:
            environ[e.key] = e.value

    jobspec = {
        "version": 1,
        "resources": args.slot_shape,
        "tasks": [
            {
                "command": args.command,
                "slot": "task",
                "count": task_count_dict,
                "attributes": {},
            }
        ],
        "attributes": {"system": {"cwd": args.dir, "environment": environ}},
    }
    jobspec["attributes"]["system"]["duration"] = args.time
    return jobspec


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


def res_tuple(s):
    l = s.split(":")
    return (l[0], int(l[1]) if len(l) > 1 else 1)


class EnvVar(object):
    def __init__(self, s):
        p = s.split("=")
        if len(p) == 1:
            self.key = s
            self.value = os.environ[s]
        else:
            self.key = p[0]
            self.value = "=".join(p[1:])


def parse_env_var(s):
    return EnvVar(s)


def get_flux_common_parser():
    """
    Shared arguments amongst flux run and submit.
    """
    flux_parser = argparse.ArgumentParser(add_help=False)
    # To detect option set or not, defaults are added in flux_jobspec
    slots = flux_parser.add_argument_group("slots")
    slots.add_argument("--nslots", type=positive_nonzero_int)
    slots.add_argument("--shape-file", type=argparse.FileType())
    slots.add_argument(
        "--slot-shape",
        type=str,
        metavar="SHAPE",
        help="slot shape, i.e. 'node/core[4]'",
    )
    tasks = flux_parser.add_argument_group("tasks")
    tasks.add_argument("--total-tasks", type=positive_nonzero_int)
    tasks.add_argument("--tasks-per-slot", type=positive_nonzero_int)
    tasks.add_argument("--tasks-per-resource", type=res_tuple)
    job = flux_parser.add_argument_group("job")
    job.add_argument("--dir", type=str, help="working directory")
    job.add_argument(
        "--time", help="time limit as a flux duration, N[smhd]", type=parse_fsd
    )
    env = flux_parser.add_argument_group("environment")
    env.add_argument(
        "--env-all", help="propagate full environment", action="store_true"
    )
    env.add_argument("--env-none", help="propagate no environment", action="store_true")
    env.add_argument(
        "-e",
        "--env",
        type=parse_env_var,
        action="append",
        help="""add ENV to the environment:
        either --env=ENV to get $ENV from the current environment or
        --env=ENV=VAL to override/set""",
    )
    flux_parser.add_argument("command", nargs=argparse.REMAINDER)
    return flux_parser


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

    flux_parser = get_flux_common_parser()
    run_parser = subparsers.add_parser(
        "run", parents=[flux_parser], help="subcommand for jobspec-style CLI arguments"
    )
    run_parser.set_defaults(func=flux_jobspec)

    args = parser.parse_args()

    if not args.command:  # list is empty
        parser.error("command is required")
        sys.exit(1)

    jobspec = args.func(args)

    if args.format == "yaml":
        out = yaml.dump(jobspec)
    else:
        out = json.dumps(jobspec)
    print(out)


if __name__ == "__main__":
    main()
