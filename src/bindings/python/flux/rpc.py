from flux.wrapper import Wrapper, WrapperPimpl
from flux.core.inner import ffi, lib, raw
import flux
import json

class RPC(WrapperPimpl):
  """An RPC state object"""
  class InnerWrapper(Wrapper):
    def __init__(self,
                 flux_handle,
                 topic,
                 payload=None,
                 nodeid=flux.FLUX_NODEID_ANY,
                 flags=0,
                 handle=None):
      # hold a reference for destructor ordering
      self.fh = flux_handle
      super(self.__class__, self).__init__(ffi, lib,
                                       handle=handle,
                                       match=ffi.typeof(lib.flux_rpc).result,
                                       prefixes=[
                                         'flux_rpc_',
                                         ],
                                       destructor=raw.flux_rpc_destroy,
                                       )
      if handle is None:
        if isinstance(flux_handle, Wrapper):
          flux_handle = flux_handle.handle

        if payload is None:
          payload = ffi.NULL
        elif payload != ffi.NULL and not isinstance(payload, basestring):
          payload = json.dumps(payload)

        if isinstance(nodeid, basestring):
          # String version is multiple nodeids
          self.handle = lib.flux_rpc_multi(flux_handle, topic, payload, nodeid, flags)
        else:
          self.handle = lib.flux_rpc(flux_handle, topic, payload, nodeid, flags)

  def __init__(self,
               flux_handle,
               topic,
               payload=None,
               nodeid=flux.FLUX_NODEID_ANY,
               flags=0,
               handle=None):
    self.pimpl = self.InnerWrapper(flux_handle,
        topic,
        payload,
        nodeid,
        flags,
        handle)

  def check(self):
    return bool(self.pimpl.check())

  def completed(self):
    return bool(self.pimpl.completed())

  def get_str(self):
    j_str = ffi.new('char *[1]')
    self.pimpl.get(j_str)
    return ffi.string(j_str[0])

  def get(self):
    return json.loads(self.get_str())

  def then(self, cb, args):
    def ThenWrapper(trash, arg):
      rpc_handle = ffi.from_handle(arg)
      cb(rpc_handle, rpc_handle.then_args)
    # Save the callback to keep it from getting collected
    self.then_cb = ffi.callback('flux_then_f',ThenWrapper)
    self.then_args = args
    return self.pimpl.then(self.then_cb, ffi.new_handle(self))

