from _kz import ffi, lib
import flux
from flux.wrapper import Wrapper, WrapperPimpl, WrapperBase
import flux.json_c as json_c
import json
import collections
import contextlib
import errno
import os
import sys

class KZWrapper(Wrapper):
  # This empty class accepts new methods, preventing accidental overloading
  # across wrappers
  pass

_raw = KZWrapper(ffi, lib, prefixes=['kz_',])
# override error check behavior for kz_get, necessary due to errno EAGAIN
_raw.kz_get.set_error_check(lambda x: False)
def generic_write(stream, string):
    if not isinstance(stream, int):
        stream.write(string)
    else:
        os.write(fd, string)


@ffi.callback('kz_ready_f')
def kz_stream_handler(kz_handle, arg):
    (stream, prefix, handle) = ffi.from_handle(arg)
    d = ffi.new('char *[1]')
    while True:
        try:
            count = _raw.get(handle, d)
            if count == 0:
                break

            if prefix is None:
                generic_write(stream, ffi.string(d[0]))
            else:
                for l in ffi.string(d[0]).splitlines(True):
                    generic_write(stream, prefix)
                    generic_write(stream, ffi.string(d[0]))
        except EnvironmentError as err:
            if err.errno == errno.EAGAIN:
                pass
            else:
                raise err

    return None


kzwatches = {}


def attach(flux_handle, key, stream, prefix=None, flags=(_raw.KZ_FLAGS_READ | _raw.KZ_FLAGS_NONBLOCK | _raw.KZ_FLAGS_NOEXIST)):
    handle = _raw.kz_open(flux_handle, key, flags)
    warg = (stream, prefix, handle)
    kzwatches[key] = warg
    return _raw.set_ready_cb(handle, kz_stream_handler, ffi.new_handle(warg))


def detach(flux_handle, key):
    (stream, arg, handle) = kzwatches.pop(key, None)
    return _raw.close(handle)

class KZStream(WrapperPimpl):
    class InnerWrapper(Wrapper):
        def __init__(self,
                flux_handle,
                name,
                flags=(_raw.KZ_FLAGS_READ | _raw.KZ_FLAGS_NONBLOCK | _raw.KZ_FLAGS_NOEXIST),
                handle=None):
            self.destroyer = _raw.kz_close
            self.handle = None
            if flux_handle is None and handle is None:  # pragma: no cover
                raise ValueError(
                    "flux_handle must be a valid Flux object or handle must be a valid kvsdir cdata pointer")
            if handle is None:
                d = ffi.new("kvsdir_t *[1]")
                _raw.kvs_get_dir(flux_handle, d, path)
                handle = _raw.kz_open(flux_handle, name, flags)

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

    def attach(self, stream=sys.stdout, fd=None):
        """ Redirect all output from this KZ stream to the specified stream"""
        arg = (stream, self.prefix)

        self.set_ready_cb(kz_stream_handler, arg)

    def __init__(self,
            flux_handle,
            name,
            flags=(_raw.KZ_FLAGS_READ | _raw.KZ_FLAGS_NONBLOCK | _raw.KZ_FLAGS_NOEXIST),
            handle=None,
            prefix=False):
        self.fh = flux_handle
        self.name = name
        if flux_handle is None and handle is None:
            raise ValueError(
                "flux_handle must be a valid Flux object or handle must be a valid kvsdir cdata pointer")
        self.pimpl = self.InnerWrapper(flux_handle, name, flags, handle)

    def __enter__(self):
        """Allow this to be used as a context manager"""
        return self

    def __exit__(self, type_arg, value, tb):
        """ When used as a context manager, the KVSDir commits itself on exit """
        self.pimpl.__del__()
        return False
