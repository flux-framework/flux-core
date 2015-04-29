from flux.wrapper import Wrapper, WrapperPimpl
from flux import core
from flux._mrpc import ffi, lib
from flux.json_c import Jobj

class MRPC(WrapperPimpl):
  class InnerWrapper(Wrapper):
    def __init__(self, flux_handle, nodelist='', handle=None):
      if handle is None:
        self.external = False
        handle = lib.flux_mrpc_create(flux_handle.handle,
            nodelist)
      else:
        self.external = True
      super(self.__class__, self).__init__(ffi, lib, handle=handle,
                                       match=ffi.typeof(lib.flux_mrpc_create).result,
                                       prefixes=['flux_mrpc_'],
                                       )
    def __del__(self):
      self.destroy()

  def __init__(self, flux_handle, nodelist='', handle=None):
    self.pimpl = self.InnerWrapper(flux_handle, nodelist=nodelist, handle=handle)

  @classmethod
  def from_event(cls, flux_handle, json_str):
    if isinstance(flux_handle, core.Flux):
      flux_handle = flux_handle.handle

    j = Jobj(json_str)
    return cls(
        flux_handle,
        handle=lib.flux_mrpc_create_fromevent(
          flux_handle,
          j.get(),
          ),
        )

  @property
  def inarg(self):
    j = Jobj()
    e = self.pimpl.get_inarg(j.get_as_dptr())
    if e != 0 or j.get() == ffi.NULL:
      raise RuntimeError("MRPC error: {}".format(e))
    return j.as_str()

  @inarg.setter
  def inarg(self, json_str):
    j = Jobj(json_str)
    self.pimpl.put_inarg(j.get())

  @property
  def outarg(self, nodeid):
    j = Jobj()
    e = self.pimpl.get_outarg(nodeid,
        j.get_as_dptr())
    if e != 0 or j.get() == ffi.NULL:
      raise RuntimeError("MRPC error: {}".format(e))
    return j.as_str()

  @outarg.setter
  def outarg(self, json_str):
    j = Jobj(json_str)
    self.pimpl.put_outarg(j.get())

  def next_outarg(self):
    return self.pimpl.next_outarg()

  def rewind_outarg(self):
    return self.pimpl.rewind_outarg()

  def respond(self):
    return self.pimpl.respond()





