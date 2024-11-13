###############################################################
# Copyright 2020 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import json
import os
import pwd
import string
import sys
import time
from collections import namedtuple
from datetime import datetime
from itertools import chain

import flux.constants
from flux.core.inner import raw
from flux.job.JobID import JobID
from flux.job.stats import JobStats
from flux.memoized_property import memoized_property
from flux.uri import JobURI

try:
    from flux.resource import SchedResourceList
except ImportError:
    SchedResourceList = None

# strsignal() is only available in Python 3.8 and up.
# flux-core's minimum is 3.6.  Use compat library if not available.
try:
    from signal import strsignal  # novermin
except ImportError:
    from flux.compat36 import strsignal


def statetostr(stateid, fmt="L"):
    return raw.flux_job_statetostr(stateid, fmt).decode("utf-8")


def statetoemoji(stateid):
    statestr = raw.flux_job_statetostr(stateid, "S").decode("utf-8")
    if statestr == "N":
        # wrapped gift
        emoji = "\U0001F381"
    elif statestr == "D":
        # stop sign
        emoji = "\U0001F6D1"
    elif statestr == "P":
        # vertical traffic light
        emoji = "\U0001F6A6"
    elif statestr == "S":
        # calendar
        emoji = "\U0001F4C5"
    elif statestr == "R":
        # person running
        emoji = "\U0001F3C3"
    elif statestr == "C":
        # wastebasket
        emoji = "\U0001F5D1"
    elif statestr == "I":
        # skull
        emoji = "\U0001F480"
    # can we output unicode to stdout? if not, return the normal short
    # string
    try:
        emoji.encode(sys.stdout.encoding)
    except UnicodeEncodeError:
        return statestr
    return emoji


def resulttostr(resultid, fmt="L"):
    # if result not returned, just return empty string back
    if resultid == "":
        return ""
    return raw.flux_job_resulttostr(resultid, fmt).decode("utf-8")


def resulttoemoji(resultid):
    if resultid != "":
        resultstr = raw.flux_job_resulttostr(resultid, "S").decode("utf-8")
        if resultstr == "CD":
            # grinning face
            emoji = "\U0001F600"
            alt = ":-)"
        elif resultstr == "F":
            # pile of poo
            emoji = "\U0001F4A9"
            alt = ":'-("
        elif resultstr == "CA":
            # collision
            emoji = "\U0001F4A5"
            alt = "%-|"
        elif resultstr == "TO":
            # hourglass done
            emoji = "\u231B"
            alt = "(-_-)"
    else:
        # ideographic space
        emoji = "\u3000"
        alt = ""
    # can we output unicode to stdout? if not, return the ascii
    try:
        emoji.encode(sys.stdout.encoding)
    except UnicodeEncodeError:
        return alt
    return emoji


# Status is the job state when pending/running (i.e. not inactive)
# status is the result when inactive
def statustostr(stateid, resultid, fmt="L"):
    if (stateid & flux.constants.FLUX_JOB_STATE_PENDING) or (
        stateid & flux.constants.FLUX_JOB_STATE_RUNNING
    ):
        statusstr = statetostr(stateid, fmt)
    else:  # flux.constants.FLUX_JOB_STATE_INACTIVE
        statusstr = resulttostr(resultid, fmt)
    return statusstr


def statustoemoji(stateid, resultid):
    if (stateid & flux.constants.FLUX_JOB_STATE_PENDING) or (
        stateid & flux.constants.FLUX_JOB_STATE_RUNNING
    ):
        emoji = statetoemoji(stateid)
    else:  # flux.constants.FLUX_JOB_STATE_INACTIVE
        emoji = resulttoemoji(resultid)
    return emoji


def get_username(userid):
    try:
        return pwd.getpwuid(userid).pw_name
    except KeyError:
        return str(userid)


class ExceptionInfo:
    def __init__(self, occurred, severity, _type, note):
        self.occurred = occurred
        self.severity = severity
        self.type = _type
        self.note = note


class EmptyObject:
    """Convenience "empty" object for use with string.format

    This class can be used in place of a real class but returns
    appropriate empty or unset value for various conversions, or
    for string.format() calls.
    """

    def __getattr__(self, attr):
        return EmptyObject()

    def __repr__(self):
        return ""

    def __str__(self):
        return ""

    def __format__(self, spec):
        # Strip trailing specifier (e.g. d, f)
        spec = spec.rstrip("bcdoxXeEfFgGn%")
        return "".__format__(spec)


# AnnotationsInfo is a wrapper for a namedtuple.  We need this
# object so that we can we detect when an attribute is missing and
# ultimately return an empty string (e.g. when an attribute does not
# exist in a namedtuple).
#
# recursive namedtuple trick inspired via
# https://stackoverflow.com/questions/1305532/convert-nested-python-dict-to-object/1305663
class AnnotationsInfo:
    def __init__(self, annotationsDict):
        self.annotationsDict = annotationsDict
        self.atuple = namedtuple("X", annotationsDict.keys())(
            *(
                AnnotationsInfo(v) if isinstance(v, dict) else v
                for v in annotationsDict.values()
            )
        )

    def __repr__(self):
        # Special case, empty dict return empty string
        if self.annotationsDict:
            return json.dumps(self.annotationsDict)
        return ""

    def __getattr__(self, attr):
        try:
            return object.__getattribute__(self.atuple, attr)
        except AttributeError:
            # We return an empty object so that we can recursively
            # handle errors.  e.g. annotations.user.illegal.illegal.illegal
            return EmptyObject()


class StatsInfo(JobStats):
    """Extend JobStats with default __repr__"""

    def __init__(self, handle=None):
        super().__init__(handle)

    def __repr__(self):
        return (
            f"PD:{self.pending} R:{self.running} "
            f"CD:{self.successful} F:{self.failed}"
        )

    def __format__(self, fmt):
        return str(self).__format__(fmt)


class InstanceInfo:
    def __init__(self, uri=None):
        self.initialized = False
        try:
            if not uri or SchedResourceList is None:
                raise ValueError
            handle = flux.Flux(str(uri))
            future = flux.resource.resource_list(handle)
            self.stats = StatsInfo(handle).update_sync()
            self.resources = future.get()
            self.initialized = True
            return
        except (ValueError, OSError, FileNotFoundError):
            self.stats = EmptyObject()
            self.resources = EmptyObject()

    @memoized_property
    def utilization(self):
        if self.initialized and self.resources.all.ncores:
            res = self.resources
            return res.allocated.ncores / res.all.ncores
        return ""

    @memoized_property
    def gpu_utilization(self):
        if self.initialized and self.resources.all.ngpus > 0:
            res = self.resources
            return res.allocated.ngpus / res.all.ngpus
        return ""

    @memoized_property
    def progress(self):
        if self.initialized:
            stats = self.stats
            if stats.total == 0:
                return ""
            return stats.inactive / stats.total
        return ""

    def __getattr__(self, attr):
        if not self.initialized:
            return ""
        return self.__getattribute__(attr)


class InfoList(list):
    """Extend list with string representation appropriate for JobInfo format"""

    def __str__(self):
        return ",".join(self)


class DependencyList(InfoList):
    """Post processed list of dependencies."""

    def __init__(self, items):
        result = []
        for dep in items:
            #  Loop through dependencies and handle special cases:
            if dep.startswith("begin-time="):
                _, eq, timestamp = dep.partition("=")
                dt = datetime.fromtimestamp(float(timestamp)).astimezone()
                if dt.date() == datetime.today().date():
                    dep = f"begin@{dt:%H:%M}"
                else:
                    dep = f"begin@{dt:%b%d-%H:%M}"
                # Only add seconds if nonzero:
                if dt.second > 0:
                    dep += f":{dt:%S}"
            result.append(dep)
        super().__init__(result)


class JobInfo:
    """
    JobInfo class: encapsulate job-list.list response in an object
    that implements a getattr interface to job information with
    memoization. Better for use with output formats since results
    are only computed as-needed.
    """

    #  Default values for job properties.
    defaults = {
        "t_depend": 0.0,
        "t_run": 0.0,
        "t_cleanup": 0.0,
        "t_inactive": 0.0,
        "duration": 0.0,
        "expiration": 0.0,
        "name": "",
        "cwd": "",
        "queue": "",
        "project": "",
        "bank": "",
        "ntasks": "",
        "ncores": "",
        "nnodes": "",
        "priority": "",
        "ranks": "",
        "nodelist": "",
        "success": "",
        "result": "",
        "waitstatus": "",
    }

    #  Other properties (used in to_dict())
    properties = (
        "id",
        "t_submit",
        "t_remaining",
        "state",
        "result",
        "username",
        "userid",
        "urgency",
        "runtime",
        "status",
        "returncode",
        "dependencies",
    )

    def __init__(self, info_resp):
        #  Set defaults, then update with job-list.list response items:
        combined_dict = self.defaults.copy()
        combined_dict.update(info_resp)

        #  Cast jobid to JobID
        combined_dict["id"] = JobID(combined_dict["id"])

        #  Rename "state" to "state_id" and "result" to "result_id"
        #  until returned state is a string:
        if "state" in combined_dict:
            combined_dict["state_id"] = combined_dict.pop("state")

        if "result" in combined_dict:
            combined_dict["result_id"] = combined_dict.pop("result")

        # Overwrite "exception" with our exception object
        exc1 = combined_dict.get("exception_occurred", "")
        exc2 = combined_dict.get("exception_severity", "")
        exc3 = combined_dict.get("exception_type", "")
        exc4 = combined_dict.get("exception_note", "")
        combined_dict["exception"] = ExceptionInfo(exc1, exc2, exc3, exc4)

        aDict = combined_dict.get("annotations", {})
        combined_dict["annotations"] = AnnotationsInfo(aDict)
        combined_dict["sched"] = combined_dict["annotations"].sched
        combined_dict["user"] = combined_dict["annotations"].user

        deps = combined_dict.get("dependencies", [])
        combined_dict["dependencies"] = DependencyList(deps)

        #  Set all keys as self._{key} to be found by getattr and
        #   memoized_property decorator:
        for key, value in combined_dict.items():
            setattr(self, "_{0}".format(key), value)

    #  getattr method to return all non-computed values in job-list.list
    #   response by default. Avoids the need to wrap @property methods
    #   that just return self._<attr>.
    #
    def __getattr__(self, attr):
        if attr.startswith("_"):
            raise AttributeError
        try:
            return getattr(self, "_{0}".format(attr))
        except (KeyError, AttributeError):
            raise AttributeError("invalid JobInfo attribute '{}'".format(attr))

    def get_instance_info(self):
        if self.uri and self.state_single == "R":  # pylint: disable=W0143
            setattr(self, "_instance", InstanceInfo(self.uri))
        else:
            setattr(self, "_instance", InstanceInfo())
        return self

    def get_runtime(self):
        if self.t_cleanup > 0 and self.t_run > 0:
            runtime = self.t_cleanup - self.t_run
        elif self.t_run > 0:
            runtime = time.time() - self.t_run
        else:
            runtime = 0.0
        return runtime

    def get_remaining_time(self):
        try:
            status = str(self.status)
            if status != "RUN":
                return 0.0
            tleft = self.expiration - time.time()
        except (KeyError, AttributeError):
            # expiration and/or status attributes may not exist, return 0.0
            return 0.0
        if tleft < 0.0:
            return 0.0
        return tleft

    @memoized_property
    def uri(self):
        if str(self.user.uri):
            return JobURI(self.user.uri)
        return None

    @property
    def t_remaining(self):
        return self.get_remaining_time()

    @memoized_property
    def state(self):
        return statetostr(self.state_id)

    @memoized_property
    def state_single(self):
        return statetostr(self.state_id, fmt="S")

    @memoized_property
    def state_emoji(self):
        return statetoemoji(self.state_id)

    @memoized_property
    def result(self):
        return resulttostr(self.result_id)

    @memoized_property
    def result_abbrev(self):
        return resulttostr(self.result_id, "S")

    @memoized_property
    def result_emoji(self):
        return resulttoemoji(self.result_id)

    @memoized_property
    def username(self):
        return get_username(self.userid)

    @memoized_property
    def runtime(self):
        return self.get_runtime()

    @memoized_property
    def status(self):
        return statustostr(self.state_id, self.result_id)

    @memoized_property
    def status_abbrev(self):
        return statustostr(self.state_id, self.result_id, fmt="S")

    @memoized_property
    def status_emoji(self):
        return statustoemoji(self.state_id, self.result_id)

    @memoized_property
    def returncode(self):
        """
        The job return code if the job has exited, or an empty string
        if the job is still active. The return code of a job is the
        highest job shell exit code, or the negative signal number if the
        job shell was terminated by a signal. For jobs that were canceled
        before the RUN state, the return code will be set to -128.
        """
        status = self.waitstatus
        code = ""
        if not isinstance(status, int):
            if self.result_id == flux.constants.FLUX_JOB_RESULT_CANCELED:
                code = -128
            elif self.result_id == flux.constants.FLUX_JOB_RESULT_FAILED:
                #  A job with empty waitstatus could fail if it received a
                #  fatal exception before starting. Use generic returncode
                #  of 1.
                code = 1
        elif os.WIFSIGNALED(status):
            code = -os.WTERMSIG(status)
        elif os.WIFEXITED(status):
            code = os.WEXITSTATUS(status)
        return code

    @memoized_property
    def contextual_info(self):
        """
        Generate contextual nodelist/reason information based on job state:
         PRIORITY: returns "priority-wait"
         DEPEND:   returns depends:dependencies list
         SCHED:    returns eta:sched.t_estimate if available, "held" if
                   urgency=0, or "priority-hold" if urgency>0 and priority=0.
         RUN+:     returns assigned nodelist
        """
        # Required for pylint, o/w it thinks state is a callable:
        state = str(self.state)
        if state == "PRIORITY":
            return "priority-wait"
        if state == "DEPEND":
            return f"depends:{self.dependencies}"
        if state == "SCHED":
            try:
                eta = self.sched.t_estimate - time.time()
                if eta < 0:
                    eta = "now"
                else:
                    eta = flux.util.fsd(eta)
                return f"eta:{eta}"
            except TypeError:
                # No eta available. Print "held" if job is held, or
                # priority-hold if priority == 0, otherwise nothing.
                if self.urgency == 0:
                    return "held"
                elif self.priority == 0:
                    return "priority-hold"
                return ""
        else:
            return self.nodelist

    @memoized_property
    def contextual_time(self):
        """
        Return job duration if job is not running, otherwise runtime
        """
        state = str(self.state)
        if state in ["PRIORITY", "DEPEND", "SCHED"]:
            return self.duration
        return self.runtime

    def to_dict(self, filtered=True):
        """
        Return a set of job attributes as a dict
        By default, empty or unset values are filtered from the result,
        so these keys will be missing. Set ``filtered=False`` to get the
        unfiltered dict, which has these uninitialized values set to
        an empty string or 0, key dependent.
        """
        result = {}
        for attr in chain(self.defaults.keys(), self.properties):
            try:
                val = getattr(self, attr)
            except AttributeError:
                val = None
            if val is not None:
                result[attr] = val

        #  The following attributes all need special handling to
        #  be converted to a dict:
        result["annotations"] = self.annotations.annotationsDict
        result["exception"] = self.exception.__dict__
        if self.uri is not None:
            result["uri"] = str(self.uri)

        if not filtered:
            return result

        #  Now clear empty/unset values to avoid confusion:
        #  - Remove any empty values (empty string).
        #  - Remove any unset timestamp values (key t_*, value 0)
        #  - Remove runtime and expiration if 0
        def zero_remove(key):
            return key.startswith("t_") or key in ("runtime", "expiration")

        for key in list(result.keys()):
            val = result[key]
            if val == "" or (zero_remove(key) and val == 0):
                del result[key]
            if key == "exception" and not val["occurred"]:
                result["exception"] = {"occurred": False}

        #  Remove duplicate annotations.user.uri if necessary:
        if self.uri is not None:
            del result["annotations"]["user"]["uri"]
            if not result["annotations"]["user"]:
                del result["annotations"]["user"]

        return result

    @memoized_property
    def inactive_reason(self):
        """
        Generate contextual exit reason based on how the job ended
        """
        state = str(self.state)
        if state != "INACTIVE":
            return ""
        result = str(self.result)
        if result == "CANCELED":
            if (
                self.exception.occurred
                and self.exception.type == "cancel"
                and self.exception.note
            ):
                return f"Canceled: {self.exception.note}"
            else:
                return "Canceled"
        elif result == "FAILED":
            # exception.type == "exec" is special case, handled by returncode
            if (
                self.exception.occurred
                and self.exception.type != "exec"
                and self.exception.severity == 0
            ):
                note = None
                if self.exception.note:
                    note = f" note={self.exception.note}"
                return f'Exception: type={self.exception.type}{note or ""}'
            elif self.returncode > 128:
                signum = self.returncode - 128
                try:
                    sigdesc = strsignal(signum)
                except ValueError:
                    sigdesc = f"Signaled {signum}"
                return sigdesc
            elif self.returncode == 126:
                return "Command invoked cannot execute"
            elif self.returncode == 127:
                return "command not found"
            elif self.returncode == 128:
                return "Invalid argument to exit"
            else:
                return f"Exit {self.returncode}"
        elif result == "TIMEOUT":
            return "Timeout"
        else:
            return f"Exit {self.returncode}"


def job_fields_to_attrs(fields):
    # Note there is no attr for "id", it is always returned
    fields2attrs = {
        "id": (),
        "id.dec": (),
        "id.hex": (),
        "id.f58": (),
        "id.f58plain": (),
        "id.emoji": (),
        "id.kvs": (),
        "id.words": (),
        "id.dothex": (),
        "userid": ("userid",),
        "username": ("userid",),
        "urgency": ("urgency",),
        "priority": ("priority",),
        "state": ("state",),
        "state_single": ("state",),
        "state_emoji": ("state",),
        "name": ("name",),
        "cwd": ("cwd",),
        "queue": ("queue",),
        "project": ("project",),
        "bank": ("bank",),
        "ntasks": ("ntasks",),
        "ncores": ("ncores",),
        "duration": ("duration",),
        "nnodes": ("nnodes",),
        "ranks": ("ranks",),
        "nodelist": ("nodelist",),
        "success": ("success",),
        "waitstatus": ("waitstatus",),
        "returncode": ("waitstatus", "result"),
        "exception.occurred": ("exception_occurred",),
        "exception.severity": ("exception_severity",),
        "exception.type": ("exception_type",),
        "exception.note": ("exception_note",),
        "result": ("result",),
        "result_abbrev": ("result",),
        "result_emoji": ("result",),
        "t_submit": ("t_submit",),
        "t_depend": ("t_depend",),
        "t_run": ("t_run",),
        "t_cleanup": ("t_cleanup",),
        "t_inactive": ("t_inactive",),
        "runtime": ("t_run", "t_cleanup"),
        "status": ("state", "result"),
        "status_abbrev": ("state", "result"),
        "status_emoji": ("state", "result"),
        "expiration": ("expiration", "state", "result"),
        "t_remaining": ("expiration", "state", "result"),
        "annotations": ("annotations",),
        "dependencies": ("dependencies",),
        "contextual_info": (
            "state",
            "dependencies",
            "annotations",
            "nodelist",
            "priority",
            "urgency",
        ),
        "contextual_time": ("state", "t_run", "t_cleanup", "duration"),
        "inactive_reason": (
            "state",
            "result",
            "waitstatus",
            "exception_occurred",
            "exception_severity",
            "exception_type",
            "exception_note",
        ),
        # Special cases, pointers to sub-dicts in annotations
        "sched": ("annotations",),
        "user": ("annotations",),
        "uri": ("annotations",),
        "uri.local": ("annotations",),
    }

    attrs = set()
    for field in fields:
        # Special case for annotations, can be arbitrary field names determined
        # by scheduler/user.
        if (
            field.startswith("annotations.")
            or field.startswith("sched.")
            or field.startswith("user.")
        ):
            attrs.update(fields2attrs["annotations"])
        elif field.startswith("instance."):
            attrs.update(fields2attrs["annotations"])
            attrs.update(fields2attrs["status"])
        else:
            attrs.update(fields2attrs[field])

    return attrs


class JobInfoFormat(flux.util.OutputFormat):
    """
    Store a parsed version of an output format string for JobInfo objects,
    allowing the fields to iterated without modifiers, building
    a new format suitable for headers display, etc...
    """

    #  List of legal format fields and their header names
    headings = {
        "id": "JOBID",
        "id.dec": "JOBID",
        "id.hex": "JOBID",
        "id.f58": "JOBID",
        "id.f58plain": "JOBID",
        "id.emoji": "JOBID",
        "id.kvs": "JOBID",
        "id.words": "JOBID",
        "id.dothex": "JOBID",
        "userid": "UID",
        "username": "USER",
        "urgency": "URG",
        "priority": "PRI",
        "state": "STATE",
        "state_single": "S",
        "state_emoji": "STATE",
        "name": "NAME",
        "cwd": "CWD",
        "queue": "QUEUE",
        "project": "PROJECT",
        "bank": "BANK",
        "ntasks": "NTASKS",
        "ncores": "NCORES",
        "duration": "DURATION",
        "nnodes": "NNODES",
        "expiration": "EXPIRATION",
        "t_remaining": "T_REMAINING",
        "ranks": "RANKS",
        "nodelist": "NODELIST",
        "success": "SUCCESS",
        "result": "RESULT",
        "result_abbrev": "RS",
        "result_emoji": "RESULT",
        "t_submit": "T_SUBMIT",
        "t_depend": "T_DEPEND",
        "t_run": "T_RUN",
        "t_cleanup": "T_CLEANUP",
        "t_inactive": "T_INACTIVE",
        "runtime": "RUNTIME",
        "status": "STATUS",
        "status_abbrev": "ST",
        "status_emoji": "STATUS",
        "waitstatus": "WSTATUS",
        "returncode": "RC",
        "exception.occurred": "EXCEPTION-OCCURRED",
        "exception.severity": "EXCEPTION-SEVERITY",
        "exception.type": "EXCEPTION-TYPE",
        "exception.note": "EXCEPTION-NOTE",
        "annotations": "ANNOTATIONS",
        "dependencies": "DEPENDENCIES",
        "contextual_info": "INFO",
        "contextual_time": "TIME",
        "inactive_reason": "INACTIVE-REASON",
        # The following are special pre-defined cases per RFC27
        "annotations.sched.t_estimate": "T_ESTIMATE",
        "annotations.sched.reason_pending": "REASON",
        "annotations.sched.resource_summary": "RESOURCES",
        "sched": "SCHED",
        "sched.t_estimate": "T_ESTIMATE",
        "sched.reason_pending": "REASON",
        "sched.resource_summary": "RESOURCES",
        "user": "USER",
        "uri": "URI",
        "uri.local": "URI",
        "instance.stats.total": "NJOBS",
        "instance.utilization": "CORE%",
        "instance.gpu_utilization": "GPU%",
        "instance.progress": "PROG",
        "instance.resources.all.ncores": "CORES",
        "instance.resources.all.ngpus": "GPUS",
        "instance.resources.all.nnodes": "NODES",
        "instance.resources.up.ncores": "UP",
        "instance.resources.up.ngpus": "UP",
        "instance.resources.up.nnodes": "UP",
        "instance.resources.down.ncores": "DOWN",
        "instance.resources.down.ngpus": "DOWN",
        "instance.resources.down.nnodes": "DOWN",
        "instance.resources.allocated.ncores": "USED",
        "instance.resources.allocated.ngpus": "USED",
        "instance.resources.allocated.nnodes": "USED",
        "instance.resources.free.ncores": "FREE",
        "instance.resources.free.ngpus": "FREE",
        "instance.resources.free.nnodes": "FREE",
    }

    def __init__(self, fmt, headings=None, prepend=None):
        """
        Parse the input format fmt with string.Formatter.
        Save off the fields and list of format tokens for later use,
        (converting None to "" in the process)

        Throws an exception if any format fields do not match the allowed
        list of headings above.

        Special case for annotations, which may be arbitrary
        creations of scheduler or user.
        """
        format_list = string.Formatter().parse(fmt)
        for _, field, _, _ in format_list:
            if field and not field in self.headings:
                if field.startswith("annotations."):
                    field_heading = field[len("annotations.") :].upper()
                    self.headings[field] = field_heading
                elif field.startswith("sched.") or field.startswith("user."):
                    field_heading = field.upper()
                    self.headings[field] = field_heading
                elif field.startswith("instance."):
                    field_heading = field[9:].upper()
                    #  Shorten RESOURCES. headings
                    if field_heading.startswith("RESOURCES."):
                        field_heading = field_heading[10:]
                    self.headings[field] = field_heading
        super().__init__(fmt)
