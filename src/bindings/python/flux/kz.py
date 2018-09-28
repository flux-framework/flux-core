import errno
import os
import sys

from _flux._kz import ffi, lib
from flux.wrapper import Wrapper, WrapperPimpl


class KZWrapper(Wrapper):
    # This empty class accepts new methods, preventing accidental overloading
    # across wrappers
    pass

RAW = KZWrapper(ffi, lib, prefixes=['kz_', ])
# override error check behavior for kz_get, necessary due to errno EAGAIN
RAW.kz_get.set_error_check(lambda x: False)


def generic_write(stream, string):
    if not isinstance(stream, int):
        stream.write(string)
    else:
        os.write(stream, string)


@ffi.callback('kz_ready_f')
def kz_stream_handler(kz_handle, arg):
    del kz_handle  # unused
    (stream, prefix, handle) = ffi.from_handle(arg)
    buf = ffi.new('char *[1]')
    while True:
        try:
            count = RAW.get(handle, buf)
            if count == 0:
                break

            if prefix is None:
                generic_write(stream, ffi.string(buf[0]))
            else:
                for _ in ffi.string(buf[0]).splitlines(True):
                    generic_write(stream, prefix)
                    generic_write(stream, ffi.string(buf[0]))
        except EnvironmentError as err:
            if err.errno == errno.EAGAIN:
                pass
            else:
                raise err

    return None


KZWATCHES = {}


def attach(flux_handle,
           key,
           stream,
           prefix=None,
           flags=(RAW.KZ_FLAGS_READ
                  | RAW.KZ_FLAGS_NONBLOCK)):
    handle = RAW.kz_open(flux_handle, key, flags)
    warg = (stream, prefix, handle)
    ffi_handle = ffi.new_handle(warg)
    KZWATCHES[key] = ffi_handle
    return RAW.set_ready_cb(handle, kz_stream_handler, ffi_handle)


def detach(flux_handle, key):
    del flux_handle  # unused
    (_, _, handle) = KZWATCHES.pop(key, None)
    return RAW.close(handle)


class KZStream(WrapperPimpl):

    class InnerWrapper(Wrapper):

        def __init__(self,
                     flux_handle,
                     name,
                     flags=(RAW.KZ_FLAGS_READ | RAW.KZ_FLAGS_NONBLOCK),
                     handle=None,
                     prefix=False):
            self.destroyer = RAW.kz_close
            self.prefix = prefix
            if flux_handle is None and handle is None:  # pragma: no cover
                raise ValueError(
                    "flux_handle must be a valid Flux object or handle must "
                    "be a valid kvsdir cdata pointer")
            if handle is None:
                handle = RAW.kz_open(flux_handle, name, flags)

            super(self.__class__, self).__init__(ffi, lib,
                                                 handle=handle,
                                                 match=ffi.typeof('kz_t *'),
                                                 prefixes=[
                                                     'kz_',
                                                 ], )

        def __del__(self):
            if self.handle is not None:
                self.destroyer(self.handle)
                self.handle = None

    def attach(self, stream=sys.stdout):
        """ Redirect all output from this KZ stream to the specified stream"""
        arg = (stream, self.prefix, self.handle)

        ffi_handle = ffi.new_handle(arg)
        KZWATCHES[self.name] = ffi_handle
        self.pimpl.set_ready_cb(kz_stream_handler, ffi_handle)

    def __init__(self,
                 flux_handle,
                 name,
                 flags=(RAW.KZ_FLAGS_READ | RAW.KZ_FLAGS_NONBLOCK),
                 handle=None,
                 prefix=None):
        super(KZStream, self).__init__()
        self.flux_handle = flux_handle
        self.prefix = prefix
        self.name = name
        if flux_handle is None and handle is None:
            raise ValueError(
                "flux_handle must be a valid Flux object or handle must be a "
                "valid kvsdir cdata pointer")
        self.pimpl = self.InnerWrapper(
            flux_handle, name, flags, handle, prefix)

    def __enter__(self):
        """Allow this to be used as a context manager"""
        return self

    def __exit__(self, type_arg, value, tb):
        """
        When used as a context manager, the KVSDir commits itself on exit
        """
        self.pimpl.__del__()
        return False
