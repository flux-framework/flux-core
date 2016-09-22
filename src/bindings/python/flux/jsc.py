from flux.wrapper import Wrapper
from flux._jsc import ffi, lib

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

_raw = JSCWrapper()


def query_jcb(flux_handle, jobid, key):
    jcb_str = ffi.new('char *[1]')
    _raw.query_jcb(flux_handle, jobid, key, jcb_str)
    if jcb_str[0] == ffi.NULL:
        return None
    else:
        return ffi.string(jcb_str[0])


def update_jcb(flux_handle, jobid, key, jcb):
    return _raw.jsc_update_jcb(flux_handle, jobid, key, jcb)


@ffi.callback('jsc_handler_f')
def JSCNotifyWrapper(jcb, arg, errnum):
    if jcb != ffi.NULL:
        jcb = ffi.string(jcb)
    cb, real_arg = ffi.from_handle(arg)
    # TODO: necessary to check errnum?
    ret = cb(jcb, real_arg, errnum)
    return ret if ret is not None else 0

handles = []


def notify_status(flux_handle, fun, arg):
    warg = (fun, arg)
    whandle = ffi.new_handle(warg)
    # TODO: another way to keep in scope to prevent GC?
    handles.append(whandle)
    return _raw.notify_status(flux_handle, JSCNotifyWrapper, whandle)


def job_num2state(job_state):
    ret = _raw.job_num2state(job_state)
    if ret == ffi.NULL:
        return None
    else:
        return ffi.string(ret)
