from flux._core import ffi, lib
from flux.wrapper import Wrapper

class Sec(Wrapper):
  def __init__(self, handle=None):
    if handle is None:
      self.external = False
      handle = lib.flux_sec_create(type_id)
    else:
      self.external = True
    super(self.__class__, self).__init__(ffi, lib, 
                                     handle=handle,
                                     match=ffi.typeof(lib.flux_sec_create).result,
                                     prefixes=['flux_sec_'],
                                     )
  
