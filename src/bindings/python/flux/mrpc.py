import json
import errno

import six

import flux.constants
from flux.core.inner import ffi, lib, raw
from flux.wrapper import Wrapper, WrapperPimpl

class MRPC(WrapperPimpl):
    """An MRPC state object"""
    class InnerWrapper(Wrapper):

        def __init__(self,
                     flux_handle,
                     topic,
                     payload=None,
                     rankset="all",
                     flags=0):
            # hold a reference for destructor ordering
            self._handle = flux_handle
            dest = raw.flux_mrpc_destroy
            super(MRPC.InnerWrapper, self).__init__(
                ffi, lib,
                handle=None,
                match=ffi.typeof(lib.flux_mrpc).result,
                prefixes=['flux_mrpc_'],
                destructor=dest)
            if isinstance(flux_handle, Wrapper):
                flux_handle = flux_handle.handle

            # Convert topic to utf-8 binary string
            if topic is None or topic == ffi.NULL:
                raise EnvironmentError(errno.EINVAL, "Topic must not be None/NULL")
            elif isinstance(topic, six.text_type):
                topic = topic.encode('UTF-8')
            elif not isinstance(topic, six.binary_type):
                raise TypeError(errno.EINVAL, "Topic must be a string, not {}".format(type(topic)))

            # Convert payload to utf-8 binary string or NULL pointer
            if payload is None or payload == ffi.NULL:
                payload = ffi.NULL
            elif isinstance(payload, six.text_type):
                payload = payload.encode('UTF-8')
            elif not isinstance(payload, six.binary_type):
                payload = json.dumps(payload, ensure_ascii=False).encode('UTF-8')

            # Validate and convert rankset to ascii binary str in proper format
            # (e.g., [0,2,3,4,9]).
            # Accepts a list of integers or digit strings (i.e., "5")
            # Also accepts the shorthands supported by the C API
            # (i.e., 'all', 'any', 'upstream')
            if isinstance(rankset, six.text_type):
                rankset = rankset.encode('ascii')
            shorthands = [b'all', b'any',  b'upstream']
            if isinstance(rankset, six.binary_type):
                if rankset not in shorthands:
                    errmsg = "Invalid rankset shorthand, must be one of {}".format(
                        shorthands)
                    raise EnvironmentError(errno.EINVAL, errmsg)
            else: # is not shorthand, should be a list of ranks
                if len(rankset) < 1:
                    raise EnvironmentError(errno.EINVAL, "Must supply at least one rank")
                elif not all([isinstance(rank, int) or rank.isdigit() for rank in rankset]):
                    raise TypeError("All ranks must be integers")
                else:
                    rankset = "[{}]".format(",".join([str(rank) for rank in rankset])).encode('ascii')

            self.handle = raw.flux_mrpc(
                flux_handle, topic, payload, rankset, flags)

    def __init__(self,
                 flux_handle,
                 topic,
                 payload=None,
                 rankset="all",
                 flags=0):
        super(MRPC, self).__init__()
        self.pimpl = self.InnerWrapper(flux_handle,
                                       topic,
                                       payload,
                                       rankset,
                                       flags)
        self.then_args = None
        self.then_cb = None

    def __iter__(self):
        return self

    def next(self):
        return self.__next__()

    # returns a tuple with the nodeid and the response payload
    def __next__(self):
        ret = self.pimpl.next()
        if ret < 0:
            raise StopIteration()
        return (self.get_nodeid(), self.get())

    def get_nodeid(self):
        nodeid = ffi.new('uint32_t [1]')
        self.pimpl.get_nodeid(nodeid)
        return int(nodeid[0])

    def get_str(self):
        j_str = ffi.new('char *[1]')
        self.pimpl.get(j_str)
        if j_str[0] == ffi.NULL:
            return None
        return ffi.string(j_str[0]).decode('utf-8')

    def get(self):
        resp_str = self.get_str()
        if resp_str is None:
            return None
        return json.loads(resp_str)

    # not strictly necessary to define, added for better autocompletion
    def check(self):
        return self.pimpl.check()
