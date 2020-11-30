###############################################################
# Copyright 2020 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import time
import pwd
import json
import string
from datetime import datetime, timedelta
from collections import namedtuple

import flux.constants
from flux.memoized_property import memoized_property
from flux.job.JobID import JobID
from flux.core.inner import raw


def statetostr(stateid, singlechar=False):
    return raw.flux_job_statetostr(stateid, singlechar).decode("utf-8")


def resulttostr(resultid, singlechar=False):
    # if result not returned, just return empty string back
    if resultid == "":
        return ""
    return raw.flux_job_resulttostr(resultid, singlechar).decode("utf-8")


def statustostr(stateid, resultid, abbrev=False):
    if stateid & flux.constants.FLUX_JOB_STATE_PENDING:
        statusstr = "PD" if abbrev else "PENDING"
    elif stateid & flux.constants.FLUX_JOB_STATE_RUNNING:
        statusstr = "R" if abbrev else "RUNNING"
    else:  # flux.constants.FLUX_JOB_STATE_INACTIVE
        statusstr = resulttostr(resultid, abbrev)
    return statusstr


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
            # We return an empty AnnotationsInfo so that we can recursively
            # handle errors.  e.g. annotations.user.illegal.illegal.illegal
            return AnnotationsInfo({})


class JobInfo:
    """
    JobInfo class: encapsulate job-info.list response in an object
    that implements a getattr interface to job information with
    memoization. Better for use with output formats since results
    are only computed as-needed.
    """

    #  Default values for job properties.
    defaults = {
        "t_depend": 0.0,
        "t_priority": 0.0,
        "t_sched": 0.0,
        "t_run": 0.0,
        "t_cleanup": 0.0,
        "t_inactive": 0.0,
        "expiration": 0.0,
        "nnodes": "",
        "ranks": "",
        "nodelist": "",
        "success": "",
        "result": "",
    }

    def __init__(self, info_resp):
        #  Set defaults, then update with job-info.list response items:
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

        #  Set all keys as self._{key} to be found by getattr and
        #   memoized_property decorator:
        for key, value in combined_dict.items():
            setattr(self, "_{0}".format(key), value)

    #  getattr method to return all non-computed values in job-info.list
    #   response by default. Avoids the need to wrap @property methods
    #   that just return self._<attr>.
    #
    def __getattr__(self, attr):
        if attr.startswith("_"):
            raise AttributeError
        try:
            return getattr(self, "_{0}".format(attr))
        except KeyError:
            raise AttributeError("invalid JobInfo attribute '{}'".format(attr))

    def get_runtime(self):
        if self.t_cleanup > 0 and self.t_run > 0:
            runtime = self.t_cleanup - self.t_run
        elif self.t_run > 0:
            runtime = time.time() - self.t_run
        else:
            runtime = 0.0
        return runtime

    def get_remaining_time(self):
        status = str(self.status)
        if status != "RUNNING":
            return 0.0
        tleft = self.expiration - time.time()
        if tleft < 0.0:
            return 0.0
        return tleft

    @property
    def t_remaining(self):
        return self.get_remaining_time()

    @memoized_property
    def state(self):
        return statetostr(self.state_id)

    @memoized_property
    def state_single(self):
        return statetostr(self.state_id, True)

    @memoized_property
    def result(self):
        return resulttostr(self.result_id)

    @memoized_property
    def result_abbrev(self):
        return resulttostr(self.result_id, True)

    @memoized_property
    def username(self):
        return get_username(self.userid)

    @memoized_property
    def runtime(self):
        return self.get_runtime()

    @memoized_property
    def status(self):
        return statustostr(self.state_id, self.result_id, False)

    @memoized_property
    def status_abbrev(self):
        return statustostr(self.state_id, self.result_id, True)


def fsd(secs):
    #  Round <1ms down to 0s for now
    if secs < 1.0e-3:
        strtmp = "0s"
    elif secs < 10.0:
        strtmp = "%.03fs" % secs
    elif secs < 60.0:
        strtmp = "%.4gs" % secs
    elif secs < (60.0 * 60.0):
        strtmp = "%.4gm" % (secs / 60.0)
    elif secs < (60.0 * 60.0 * 24.0):
        strtmp = "%.4gh" % (secs / (60.0 * 60.0))
    else:
        strtmp = "%.4gd" % (secs / (60.0 * 60.0 * 24.0))
    return strtmp


class JobInfoFormat(flux.util.OutputFormat):
    """
    Store a parsed version of an output format string for JobInfo objects,
    allowing the fields to iterated without modifiers, building
    a new format suitable for headers display, etc...
    """

    class JobFormatter(string.Formatter):
        # pylint: disable=too-many-branches
        def convert_field(self, value, conv):
            """
            Flux job-specific field conversions. Avoids the need
            to create many different format field names to represent
            different conversion types. (mainly used for time-specific
            fields for now).
            """
            orig_value = str(value)
            if conv == "d":
                # convert from float seconds since epoch to a datetime.
                # User can than use datetime specific format fields, e.g.
                # {t_inactive!d:%H:%M:%S}.
                try:
                    value = datetime.fromtimestamp(value)
                except TypeError:
                    if orig_value == "":
                        value = datetime.fromtimestamp(0.0)
                    else:
                        raise
            elif conv == "D":
                # As above, but convert to ISO 8601 date time string.
                try:
                    value = datetime.fromtimestamp(value).strftime("%FT%T")
                except TypeError:
                    if orig_value == "":
                        value = ""
                    else:
                        raise
            elif conv == "F":
                # convert to Flux Standard Duration (fsd) string.
                try:
                    value = fsd(value)
                except TypeError:
                    if orig_value == "":
                        value = ""
                    else:
                        raise
            elif conv == "H":
                # if > 0, always round up to at least one second to
                #  avoid presenting a nonzero timedelta as zero
                try:
                    if 0 < value < 1:
                        value = 1
                    value = str(timedelta(seconds=round(value)))
                except TypeError:
                    if orig_value == "":
                        value = ""
                    else:
                        raise
            else:
                value = super().convert_field(value, conv)
            return value

        def format_field(self, value, spec):
            if spec.endswith("h"):
                basecases = ("", "0s", "0.0", "0:00:00", "1970-01-01T00:00:00")
                value = "-" if str(value) in basecases else str(value)
                spec = spec[:-1] + "s"
            return super().format_field(value, spec)

    class HeaderFormatter(JobFormatter):
        """Custom formatter for flux-jobs(1) header row.

        Override default formatter behavior of calling getattr() on dotted
        field names. Instead look up header literally in kwargs.
        This greatly simplifies header name registration as well as
        registration of "valid" fields.
        """

        def get_field(self, field_name, args, kwargs):
            """Override get_field() so we don't do the normal gettatr thing"""
            if field_name in kwargs:
                return kwargs[field_name], None
            return super().get_field(field_name, args, kwargs)

    #  List of legal format fields and their header names
    headings = {
        "id": "JOBID",
        "id.dec": "JOBID",
        "id.hex": "JOBID",
        "id.f58": "JOBID",
        "id.kvs": "JOBID",
        "id.words": "JOBID",
        "id.dothex": "JOBID",
        "userid": "UID",
        "username": "USER",
        "admin_priority": "PRI",
        "queue_priority": "QPRI",
        "state": "STATE",
        "state_single": "S",
        "name": "NAME",
        "ntasks": "NTASKS",
        "nnodes": "NNODES",
        "expiration": "EXPIRATION",
        "t_remaining": "T_REMAINING",
        "ranks": "RANKS",
        "nodelist": "NODELIST",
        "success": "SUCCESS",
        "result": "RESULT",
        "result_abbrev": "RS",
        "t_submit": "T_SUBMIT",
        "t_depend": "T_DEPEND",
        "t_priority": "T_PRIORITY",
        "t_sched": "T_SCHED",
        "t_run": "T_RUN",
        "t_cleanup": "T_CLEANUP",
        "t_inactive": "T_INACTIVE",
        "runtime": "RUNTIME",
        "status": "STATUS",
        "status_abbrev": "ST",
        "exception.occurred": "EXCEPTION-OCCURRED",
        "exception.severity": "EXCEPTION-SEVERITY",
        "exception.type": "EXCEPTION-TYPE",
        "exception.note": "EXCEPTION-NOTE",
        "annotations": "ANNOTATIONS",
        # The following are special pre-defined cases per RFC27
        "annotations.sched.t_estimate": "T_ESTIMATE",
        "annotations.sched.reason_pending": "REASON",
        "annotations.sched.resource_summary": "RESOURCES",
        "sched": "SCHED",
        "sched.t_estimate": "T_ESTIMATE",
        "sched.reason_pending": "REASON",
        "sched.resource_summary": "RESOURCES",
        "user": "USER",
    }

    def __init__(self, fmt):
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
        for (_, field, _, _) in format_list:
            if field and not field in self.headings:
                if field.startswith("annotations."):
                    field_heading = field[len("annotations.") :].upper()
                    self.headings[field] = field_heading
                elif field.startswith("sched.") or field.startswith("user."):
                    field_heading = field.upper()
                    self.headings[field] = field_heading
        super().__init__(self.headings, fmt, prepend="0.")

    def format(self, obj):
        """
        format object with our JobFormatter
        """
        return self.JobFormatter().format(self.get_format(), obj)

    def header(self):
        """
        format header with custom HeaderFormatter
        """
        return self.HeaderFormatter().format(self.header_format(), **self.headings)
