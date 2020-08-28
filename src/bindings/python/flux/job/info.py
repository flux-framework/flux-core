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
    if stateid & flux.constants.FLUX_JOB_PENDING:
        statusstr = "PD" if abbrev else "PENDING"
    elif stateid & flux.constants.FLUX_JOB_RUNNING:
        statusstr = "R" if abbrev else "RUNNING"
    else:  # flux.constants.FLUX_JOB_INACTIVE
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
        "t_sched": 0.0,
        "t_run": 0.0,
        "t_cleanup": 0.0,
        "t_inactive": 0.0,
        "expiration": 0.0,
        "nnodes": "",
        "ranks": "",
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
