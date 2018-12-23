import importlib
from _flux._core import lib
from flux.core.handle import Flux


def mod_main_trampoline(name, int_handle, args):
    # generate a flux wrapper class instance from the handle
    flux_instance = Flux(handle=lib.unpack_long(int_handle))
    user_mod = None
    try:
        user_mod = importlib.import_module(
            'flux.modules.' + name, 'flux.modules')
    except ImportError:  # check user paths for the module
        user_mod = importlib.import_module(name)

    # call into mod_main with a flux class instance and the argument dict
    # it might be more pythonic to unpack the args as keyword/positional
    # arguments to this function, but I think this is cleaner for now
    user_mod.mod_main(flux_instance, *args)
