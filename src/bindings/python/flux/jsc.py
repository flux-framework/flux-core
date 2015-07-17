from flux.wrapper import Wrapper, WrapperPimpl
from flux._jsc import ffi, lib

class JSCWrapper(Wrapper):
  """ 
  Generic JSC wrapper, you probably do not want or need one of these.
  """

  def __init__(self):
    """Set up the wrapper interface for functions prefixed with jsc_"""
    super(JSCWrapper, self).__init__(ffi,
        lib,
        prefixes=[
          'jsc_',
          ])

raw = JSCWrapper()

