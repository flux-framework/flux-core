from flux._barrier import ffi, lib
from flux.wrapper import WrapperBase

def barrier(flux_handle, name, nprocs):
  if isinstance(flux_handle, WrapperBase):
    flux_handle = flux_handle.handle
  return lib.flux_barrier(flux_handle, name, nprocs)
