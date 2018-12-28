from _flux._core import ffi, lib
from flux.wrapper import Wrapper


class Core(Wrapper):
    """
    Generic Core wrapper, you probably do not want or need one of these.
    """

    def __init__(self):
        """Set up the wrapper interface for functions prefixed with flux_"""
        super(Core, self).__init__(ffi, lib, prefixes=["flux_", "FLUX_"])


# keeping this for compatibility
# pylint: disable=invalid-name
raw = Core()
