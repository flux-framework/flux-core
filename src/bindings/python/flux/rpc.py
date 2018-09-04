import json
import six
from flux.wrapper import Wrapper, WrapperPimpl
from flux.core.inner import ffi, lib, raw
import flux.constants


class RPC(WrapperPimpl):
    """An RPC state object"""
    class InnerWrapper(Wrapper):

        def __init__(self,
                     flux_handle,
                     topic,
                     payload=None,
                     nodeid=flux.constants.FLUX_NODEID_ANY,
                     flags=0,
                     handle=None):
            # hold a reference for destructor ordering
            self._handle = flux_handle
            dest = raw.flux_future_destroy
            super(self.__class__, self).__init__(ffi, lib,
                                                 handle=handle,
                                                 match=ffi.typeof(
                                                     lib.flux_rpc).result,
                                                 prefixes=[
                                                     'flux_rpc_',
                                                 ],
                                                 destructor=dest,)
            if handle is None:
                if isinstance(flux_handle, Wrapper):
                    flux_handle = flux_handle.handle


                if payload is None or payload == ffi.NULL:
                    payload = ffi.NULL
                elif not (isinstance(payload, six.binary_type)):
                    payload = json.dumps(payload)

                if isinstance(nodeid, six.binary_type):
                    # String version is multiple nodeids
                    self.handle = raw.flux_rpc_multi(
                        flux_handle, topic, payload, nodeid, flags)
                else:
                    self.handle = raw.flux_rpc(
                        flux_handle, topic, payload, nodeid, flags)

    def __init__(self,
                 flux_handle,
                 topic,
                 payload=None,
                 nodeid=flux.constants.FLUX_NODEID_ANY,
                 flags=0,
                 handle=None):
        super(RPC, self).__init__()
        self.pimpl = self.InnerWrapper(flux_handle,
                                       topic,
                                       payload,
                                       nodeid,
                                       flags,
                                       handle)
        self.then_args = None
        self.then_cb = None



    def get_str(self):
        j_str = ffi.new('char *[1]')
        self.pimpl.get(j_str)
        return ffi.string(j_str[0])

    def get(self):
        return json.loads(self.get_str())

