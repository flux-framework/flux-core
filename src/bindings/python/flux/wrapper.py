import re
import errno
import os
from types import MethodType

# A do-nothing class for checking for composition-based wrappers that offer a
# handle attribute
class WrapperBase(object):
  def __init__(self):
    self._handle = None

  @property
  def handle(self):
    return self._handle

  @handle.setter
  def handle(self, handle):
    self._handle = handle

class WrapperPimpl(WrapperBase):
  @property
  def handle(self):
    return self.pimpl.handle

  @handle.setter
  def handle(self, handle):
    self.pimpl.handle = handle

class Wrapper(WrapperBase):
  """
  Forms a wrapper around an interface that dynamically searches for undefined
  names, and can detect and pass a handle argument of specified type when it
  is found in the signature of an un-specified target function.
  """
  
  def __init__(self,
               ffi,
               lib,
               handle=None,
               match=None,
               filter_match=True,
               prefixes=[],
               ):
    super(Wrapper, self).__init__()

    self.ffi = ffi
    self.lib = lib
    self.handle = handle
    self.match = match
    self.filter_match = filter_match
    self.prefixes = prefixes

    self.NULL = ffi.NULL

  def callback(self, type_id):
    """ Pass-through to cffi callback mechanism for now"""
    return self.ffi.callback(type_id)

  def check_handle(self, name, t):
    if self.match is not None and self.handle is not None:
      if t.kind == 'function' and t.args[0] == self.match: #first argument is of handle type
        return True
      else:
        if self.filter_match:
          raise AttributeError("Flux Wrapper object masks function {} type: {} match: {}".format(name, t, self.match))
    return False

  def check_wrap(self, fun, name):
    try: #absorb the error if it is a basic type
      t = self.ffi.typeof(fun)
    except TypeError:
        return fun
    arg_trans = []
    # If this function takes at least one pointer
    if t.kind != 'function':
      return fun

    add_handle = self.check_handle(name, t)
    alist = t.args[1:] if add_handle else t.args
    for i, a in enumerate(alist, start=1 if add_handle else 0):
      if a.kind == 'array' or a.kind == 'pointer':
        arg_trans.append(i)
    # print fun.__name__, 'handler?', add_handle, t.kind, t.args
    # print alist, 'arg_trans', arg_trans
    holder = fun
    def NoneWrapper(self_in, *args_in): #keyword args are not supported
      # print holder.__name__, 'got', self_in, args_in
      args = []
      if add_handle:
        args.append(self_in.handle)
      args.extend(args_in)
      for i in arg_trans:
        if args[i] is None:
          args[i] = self.ffi.NULL
        elif isinstance(args[i], WrapperBase):
          # Unpack wrapper objects
          args[i] = args[i].handle
      self_in.ffi.errno = 0
      result = holder(*args)
      # Convert errno errors into python exceptions
      err = self_in.ffi.errno
      if err != 0:
        raise EnvironmentError(err, os.strerror(err))
      if result == self_in.ffi.NULL:
        return None
      else:
        return result
      # elif t.result == self.ffi.typeof('char *'):
      #   return self.ffi.string(result)
    fun = NoneWrapper
    return fun

  def __getattr__(self, name):
    fun = None
    #try it bare
    try:
        fun = getattr(self.lib,name)
    except AttributeError:
        pass
    for prefix in self.prefixes:
      try:
          #wrap it
          fun = getattr(self.lib, prefix + name)
          break
      except AttributeError:
          pass
    if fun is None:
      # Do it again to get a good error
      getattr(self.lib, name)
    fun = self.check_wrap(fun, name)
    # Store the wrapper function into the class to prevent a second lookup
    setattr(self.__class__, name, fun)
    return getattr(self, name)
