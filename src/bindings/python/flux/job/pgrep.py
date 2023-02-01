##############################################################
# Copyright 2023 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import re
import sys
from datetime import datetime

from flux.constraint.parser import ConstraintLexer, ConstraintParser
from flux.core.inner import ffi, raw
from flux.hostlist import Hostlist
from flux.idset import IDset
from flux.job import JobID
from flux.util import parse_datetime, parse_fsd


class Conditional:
    name = None

    def __init__(self, values):
        self.values = [PgrepConstraint(value) for value in values]

    def dict(self):
        return {self.name: [x.dict() for x in self.values]}


class And(Conditional):
    name = "and"

    def match(self, job):
        return all(constraint.match(job) for constraint in self.values)


class Or(Conditional):
    name = "or"

    def match(self, job):
        return any(constraint.match(job) for constraint in self.values)


class Not(Conditional):
    name = "not"

    def match(self, job):
        return all(not constraint.match(job) for constraint in self.values)


class JobName:
    def __init__(self, values):
        self.values = [re.compile(value) for value in values]

    def match(self, job):
        return all(re.search(job.name) for re in self.values)

    def dict(self):
        return {"name": [re.pattern for re in self.values]}


class HostList:
    def __init__(self, values):
        self.hostlist = Hostlist(values)

    def match(self, job):
        return any(host in self.hostlist for host in Hostlist(job.nodelist))

    def dict(self):
        return {"hostlist": [self.hostlist.encode()]}


class Ranks:
    def __init__(self, values):
        self.idset = IDset(values)

    def match(self, job):
        return any(rank in self.idset for rank in IDset(job.ranks))

    def dict(self):
        return {"ranks": [self.idset.encode()]}


class JobException:
    def __init__(self, values):
        self.types = [re.compile(value) for value in values]

    def match(self, job):
        exc_type = getattr(job, "exception_type", None)
        if exc_type is None:
            return False
        return any(re.search(exc_type) for re in self.types)

    def dict(self):
        return {"exception": [re.pattern for re in self.types]}


class JobIs:
    supported_values = {
        "started": lambda job: job.runtime > 0,
        "running": lambda job: job.state.lower() in ["run", "cleanup"],
        "pending": lambda x: x.state.lower() in ["depend", "priority", "sched"],
        "complete": lambda job: job.state.lower() in ["cleanup", "inactive"],
        "success": lambda job: job.success,
        "failure": lambda job: not job.success,
        "failed": lambda job: not job.success,
    }

    def __init__(self, values):
        self.values = [self.check(x) for x in values]

    def check(self, value):
        value = value.lower()
        if value not in self.supported_values:
            try:
                raw.flux_job_strtostate(value, ffi.NULL)
            except OSError:
                try:
                    raw.flux_job_strtoresult(value, ffi.NULL)
                except OSError:
                    raise ValueError(f"invalid value in 'is': {value}")
        return value

    def match(self, job):
        for value in self.values:
            if value in self.supported_values:
                if self.supported_values[value](job):
                    return True
            elif value == job.state.lower():
                return True
            elif job.result and value == job.result.lower():
                return True
        return False

    def dict(self):
        return {"is": self.values}


class Comparison:
    def __init__(self, values, attr="id", conv=None):
        self.attr = attr
        if not getattr(self, "convert", None):
            self.convert = conv
        self.op = ""
        value = values[0]

        if value.startswith(">=") or value.startswith("+="):
            self.op = ">="
            self.match = self.ge
            self.value = self.convert(value[2:])
        elif value.startswith("<=") or value.startswith("-="):
            self.op = "<="
            self.match = self.le
            self.value = self.convert(value[2:])
        elif value.startswith("<") or value.startswith("-"):
            self.op = value[:1]
            self.match = self.lt
            self.value = self.convert(value[1:])
        elif value.startswith(">") or value.startswith("+"):
            self.op = value[:1]
            self.match = self.gt
            self.value = self.convert(value[1:])
        elif ".." in value:
            start, end = value.split("..")
            if start and end:
                self.op = "range"
                self.match = self.range
                self.value = list(map(self.convert, [start, end]))
            elif start:
                self.op = ">="
                self.match = self.ge
                self.value = self.convert(start)
            elif end:
                self.op = "<="
                self.match = self.le
                self.value = self.convert(end)
        else:
            self.op = ""
            self.match = self.eq
            self.value = self.convert(value)

    def dict(self):
        if self.op == "range":
            return {self.attr: [f"{self.value[0]}..{self.value[1]}"]}
        return {self.attr: [f"{self.op}{self.value}"]}

    def ge(self, job):
        val = self.convert(getattr(job, self.attr))
        return val >= self.value

    def le(self, job):
        val = self.convert(getattr(job, self.attr))
        return val <= self.value

    def lt(self, job):
        val = self.convert(getattr(job, self.attr))
        return val < self.value

    def gt(self, job):
        val = self.convert(getattr(job, self.attr))
        return val > self.value

    def eq(self, job):
        val = self.convert(getattr(job, self.attr))
        return val == self.value

    def range(self, job):
        val = self.convert(getattr(job, self.attr))
        return self.value[0] <= val <= self.value[1]


class Duration(Comparison):
    def convert(self, duration):
        if isinstance(duration, str):
            return parse_fsd(duration)
        if isinstance(duration, (float, int)):
            return float(duration)
        raise ValueError(f"Unknown duration type for '{duration}'")


class Datetime(Comparison):
    def convert(self, dt):
        if isinstance(dt, (float, int)):
            if dt == 0:
                # A datetime of zero indicates unset, or an arbitrary time
                # in the future. Return 12 months from now.
                return parse_datetime("+12m")
            return datetime.fromtimestamp(dt).astimezone()
        else:
            return parse_datetime(dt, assumeFuture=False)


class JobExitCode(Comparison):
    def __init__(self, values):
        value = values[0]
        #  Convert exitcode to a waitstatus. Start at first numeric
        #  character since value may start with >, >=, etc.:
        match = re.search(r"\d", value)
        prefix = value[: match.start()]
        wstatus = int(value[match.start() :]) << 8
        values = [f"{prefix}{wstatus}"]
        super().__init__(values, attr="waitstatus", conv=int)


class PgrepConstraint:
    operations = {
        "and": And,
        "or": Or,
        "not": Not,
        "is": JobIs,
        "name": JobName,
        "ranks": Ranks,
        "hostlist": HostList,
        "exception": JobException,
        "exitcode": JobExitCode,
        "jobid": lambda values: Comparison(values, "id", JobID),
        "wstatus": lambda values: Comparison(values, "waitstatus", int),
        "urgency": lambda values: Comparison(values, "urgency", int),
        "ntasks": lambda values: Comparison(values, "ntasks", int),
        "nnodes": lambda values: Comparison(values, "nnodes", int),
        "ncores": lambda values: Comparison(values, "ncores", int),
        "userid": lambda values: Comparison(values, "userid", int),
        "priority": lambda values: Comparison(values, "priority", int),
        "runtime": lambda values: Duration(values, "runtime"),
        "duration": lambda values: Duration(values, "duration"),
        "remaining": lambda values: Duration(values, "t_remaining"),
        "start": lambda values: Datetime(values, "t_run"),
        "end": lambda values: Datetime(values, "t_cleanup"),
        "inactive": lambda values: Datetime(values, "t_inactive"),
        "submitted": lambda values: Datetime(values, "t_submit"),
        "expiration": lambda values: Datetime(values, "expiration"),
    }

    def __init__(self, term):
        operator = list(term)[0]
        if operator not in self.operations:
            raise ValueError(f"Unknown operation {operator}")
        values = term[operator]
        self.constraint = self.operations[operator](values)

    def match(self, job):
        return self.constraint.match(job)

    def dict(self):
        return self.constraint.dict()


class PgrepConstraintParser(ConstraintParser):
    operator_map = {
        None: "name",
        "submit": "submitted",
        "id": "jobid",
        "host": "hostlist",
        "hosts": "hostlist",
        "rank": "ranks",
    }
    split_values = {"is": ","}

    @staticmethod
    def convert_token(arg):
        if arg.startswith("@"):
            arg = arg[1:]
            if ".." in arg:
                start, end = arg.split("..")
                arg = "(not ("
                if start:
                    arg += f"'end:<{start}'"
                if start and end:
                    arg += " or "
                if end:
                    arg += f"'start:>{end}'"
                arg += "))"
            else:
                arg = f"(start:'<={arg}' and end:'>={arg}')"
        elif ":" not in arg and ".." in arg:
            # X..Y where X and Y are jobids is translated to a
            # range of jobids
            start, end = arg.split("..")
            try:
                if start:
                    JobID(start)
                if end:
                    JobID(end)
                arg = f"'jobid:{arg}'"
            except ValueError:
                arg = f"'name:{arg}'"
        else:
            arg = f"'{arg}'"
        return arg + " "

    def parse(self, string, debug=False):
        #  First pass: traverse all tokens and make any convenience conversions
        expression = ""
        lexer = ConstraintLexer()
        lexer.input(string)
        while True:
            tok = lexer.token()
            if tok is None:
                break
            if debug:
                print(tok, file=sys.stderr)
            if tok.type != "TOKEN":
                if tok.type in ["LPAREN", "RPAREN", "NEGATE"]:
                    expression += tok.value
                else:
                    expression += f"{tok.value} "
            else:
                expression += self.convert_token(tok.value)

        expression = expression.strip()
        if debug:
            print(f"expression: {expression}", file=sys.stderr)
        return super().parse(expression)
