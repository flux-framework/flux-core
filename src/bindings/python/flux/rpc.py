from flux.wrapper import Wrapper, WrapperPimpl
from flux._core import ffi, lib
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
      self.external = False
      if handle is not None:
        self.external = True
      else:
        if isinstance(flux_handle, Wrapper):
          flux_handle = flux_handle.handle

        if payload is None:
          payload = ffi.NULL
        elif payload != ffi.NULL and not isinstance(payload, basestring):
          payload = json.dumps(payload)

        if isinstance(nodeid, basestring):
          # String version is multiple nodeids
          handle = lib.flux_rpc_multi(flux_handle, topic, payload, nodeid, flags)
        else:
          handle = lib.flux_rpc(flux_handle, topic, payload, nodeid, flags)

      
      super(self.__class__, self).__init__(ffi, lib, 
                                       handle=handle,
                                       match=ffi.typeof(lib.flux_rpc).result,
                                       prefixes=[
                                         'flux_rpc_',
                                         ],
                                       )
    def __del__(self):
      pass
      # if not self.external and self.handle is not None:
      #   lib.flux_rpc_destroy(self.handle)

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
    c_nodeid = ffi.new('uint32_t [1]')
    j_str = ffi.new('char *[1]')
    self.pimpl.get(c_nodeid, j_str)
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

