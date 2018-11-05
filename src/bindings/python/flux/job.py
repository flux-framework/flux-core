import errno

import six

from flux.wrapper import Wrapper
from flux.util import check_future_error
from _flux._job import ffi, lib

class JobWrapper(Wrapper):
    def __init__(self):
        super(JobWrapper, self).__init__(ffi, lib, prefixes=['flux_job_',])

RAW = JobWrapper()

def submit_async(flux_handle, jobspec,
                 priority=lib.FLUX_JOB_PRIORITY_DEFAULT,
                 flags=0):
    if isinstance(jobspec, six.text_type):
        jobspec = jobspec.encode('utf-8')
    elif jobspec is None or jobspec == ffi.NULL:
        # catch this here rather than in C for a better error message
        raise EnvironmentError(errno.EINVAL, "jobspec must not be None/NULL")
    elif not isinstance(jobspec, six.binary_type):
        raise TypeError("jobpsec must be a string (either binary or unicode)")

    future = RAW.submit(flux_handle, jobspec, priority, flags)
    return future

@check_future_error
def submit_get_id(future):
    jobid = ffi.new('flux_jobid_t[1]')
    RAW.submit_get_id(future, jobid)
    return int(jobid[0])

def submit(flux_handle, jobspec,
           priority=lib.FLUX_JOB_PRIORITY_DEFAULT,
           flags=0):
    future = submit_async(flux_handle, jobspec, priority, flags)
    return submit_get_id(future)
