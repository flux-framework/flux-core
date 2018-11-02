import json
import re
import six
import sys

from flux.wrapper import Wrapper
from _flux._core import ffi, lib

thismodule = sys.modules[__name__]
# Inject enum/define names matching ^JSC_[A-Z_]+$ into module
PATTERN = re.compile("^JSC_[A-Z_]+")
for k in dir(lib):
    if PATTERN.match(k):
        v = ffi.string(getattr(lib, k)).decode("ascii")
        print("adding", k, v)
        setattr(thismodule, k, v)

class JSCWrapper(Wrapper):
    """
    Generic JSC wrapper
    """

    def __init__(self):
        """Set up the wrapper interface for functions prefixed with jsc_"""
        super(JSCWrapper, self).__init__(ffi,
                                         lib,
                                         prefixes=[
                                             'jsc_',
                                         ])

RAW = JSCWrapper()


def query_jcb(flux_handle, jobid, key):
    jcb_str = ffi.new('char *[1]')
    RAW.query_jcb(flux_handle, jobid, key, jcb_str)
    if jcb_str[0] == ffi.NULL:
        return None
    return json.loads(ffi.string(jcb_str[0]).decode('utf-8'))


def update_jcb(flux_handle, jobid, key, jcb):
    # TODO: validate the key is one of the constants in jstatctl.h
    if isinstance(jcb, six.text_type):
        jcb = jcb.encode('utf-8')
    elif not isinstance(jcb, six.binary_type):
        jcb = json.dumps(jcb).encode('utf-8')
    return RAW.jsc_update_jcb(flux_handle, jobid, key, jcb)


@ffi.callback('jsc_handler_f')
def jsc_notify_wrapper(jcb, arg, errnum):
    if jcb != ffi.NULL:
        jcb = ffi.string(jcb).decode('utf-8')
    callback, real_arg = ffi.from_handle(arg)
    ret = callback(jcb, real_arg, errnum)
    return ret if ret is not None else 0

HANDLES = []


def notify_status(flux_handle, fun, arg):
    warg = (fun, arg)
    whandle = ffi.new_handle(warg)
    HANDLES.append(whandle)
    return RAW.notify_status(flux_handle, jsc_notify_wrapper, whandle)


def job_num2state(job_state):
    ret = RAW.job_num2state(job_state)
    if ret == ffi.NULL:
        return None
    return ret.decode('ascii')

def job_state2num(job_state):
    if isinstance(job_state, six.text_type):
        # jsc doesn't use utf-8 internally, catch erroneous unicode here
        job_state.encode('ascii')
    return RAW.job_state2num(job_state)
