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


class FunctionWrapper(object):
    def __init__(self, fun, name, t, ffi, add_handle=False):
        self.arg_trans = []
        self.fun = fun
        self.add_handle = add_handle
        self.build_argument_translation_list(t)
        self.is_error = lambda x: False
        if t.result.kind in ('pointer', 'array'):
            self.is_error = lambda x: x is None
        elif t.result in [ffi.typeof(x)
                          for x in ('char', 'int', 'short', 'long',
                                    'long long', 'int32_t', 'int64_t')]:
            self.is_error = lambda x: x < 0

    def build_argument_translation_list(self, t):
        alist = t.args[1:] if self.add_handle else t.args
        for i, a in enumerate(alist, start=1 if self.add_handle else 0):
            if a.kind == 'array' or a.kind == 'pointer':
                self.arg_trans.append(i)

    def __call__(self, calling_object, *args_in):
        # print holder.__name__, 'got', calling_object, args_in
        args = []
        if self.add_handle:
            args.append(calling_object.handle)
        args.extend(args_in)
        for i in self.arg_trans:
            if args[i] is None:
                args[i] = calling_object.ffi.NULL
            elif isinstance(args[i], WrapperBase):
                # Unpack wrapper objects
                args[i] = args[i].handle

        calling_object.ffi.errno = 0

        result = self.fun(*args)

        if result == calling_object.ffi.NULL:
            result = None

        # Convert errno errors into python exceptions
        err = calling_object.ffi.errno

        if self.is_error(result) and err != 0:
            raise EnvironmentError(err, os.strerror(err))

        return result


class Wrapper(WrapperBase):
    """
    Forms a wrapper around an interface that dynamically searches for undefined
    names, and can detect and pass a handle argument of specified type when it
    is found in the signature of an un-specified target function.
    """

    def __init__(self, ffi, lib,
                 handle=None,
                 match=None,
                 filter_match=True,
                 prefixes=[], ):
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
            if t.kind == 'function' and t.args[
                0
            ] == self.match:  #first argument is of handle type
                return True
            else:
                if self.filter_match:
                    raise AttributeError(
                        "Flux Wrapper object masks function {} type: {} match: {}".format(
                            name, t, self.match))
        return False

    def check_wrap(self, fun, name):
        try:  #absorb the error if it is a basic type
            t = self.ffi.typeof(fun)
        except TypeError:
            return fun

        if not callable(fun) or t.kind != 'function':
            return fun

        return FunctionWrapper(fun, name, t, self.ffi,
                               add_handle=self.check_handle(name, t))

    def __getattr__(self, name):
        fun = None
        #try it bare
        try:
            fun = getattr(self.lib, name)
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
        new_method = MethodType(self.check_wrap(fun, name), None,
                                self.__class__)
        # Store the wrapper function into the class to prevent a second lookup
        setattr(self.__class__, name, new_method)
        return getattr(self, name)
