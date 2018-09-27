import json
import six

from flux.wrapper import Wrapper
from _flux._jsc import ffi, lib

# Constants taken from jstatctl.h
JSC_STATE_PAIR = "state-pair"
JSC_STATE_PAIR_OSTATE = "ostate"
JSC_STATE_PAIR_NSTATE = "nstate"
JSC_RDESC = "rdesc"
JSC_RDESC_NNODES = "nnodes"
JSC_RDESC_NTASKS = "ntasks"
JSC_RDESC_WALLTIME = "walltime"
JSC_RDL = "rdl"
JSC_RDL_ALLOC = "rdl_alloc"
JSC_RDL_ALLOC_CONTAINED = "contained"
JSC_RDL_ALLOC_CONTAINING_RANK = "cmbdrank"
JSC_RDL_ALLOC_CONTAINED_NCORES = "cmbdncores"
JSC_PDESC = "pdesc"
JSC_PDESC_SIZE = "procsize"
JSC_PDESC_HOSTNAMES = "hostnames"
JSC_PDESC_EXECS = "executables"
JSC_PDESC_PDARRAY = "pdarray"
JSC_PDESC_RANK_PDARRAY_PID = "pid"
JSC_PDESC_RANK_PDARRAY_HINDX = "hindx"
JSC_PDESC_RANK_PDARRAY_EINDX = "eindx"


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
    else:
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
    else:
        return ret.decode('ascii')
