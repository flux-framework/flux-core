import re
import errno
import os
import inspect
import cffi
from types import MethodType


class MissingFunctionError(Exception):
    def __init__(self, name, c_name, name_list, arguments):

        call_stack = inspect.stack()
        caller = inspect.getframeinfo(call_stack[2][0])

        message = """
A non-existant or unavailable function invocation has been detected.
Has this function been recently removed or renamed?

Name called: {name}
Possible C names: {name_list}
Arguments: {arguments}
Likely intended C invokation: {c_name}{arguments}
Invocation detail: inside function {outer}
{file}:{line}: {context}
        """.format(
            name=name,
            name_list=name_list,
            arguments=arguments,
            file=caller.filename,
            line=caller.lineno,
            context=caller.code_context,
            outer=caller.function,
            c_name=c_name, )
        # Call the base class constructor with the parameters it needs
        super(MissingFunctionError, self).__init__(message)


class ErrorPrinter(object):
    def __init__(self, name, prefixes):
        self.name = name
        self.prefixes = prefixes

    def __call__(self, *args):
        c_name = self.name
        if not any([re.search('^' + x, self.name) for x in self.prefixes]):
            c_name = self.prefixes[0] + self.name

        raise MissingFunctionError(self.name, c_name, [p + self.name
                                                       for p in self.prefixes],
                                   args)


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

    def __enter__(self):
        return self

    def __exit__(self, type_arg, value, tb):
        self.pimpl.__exit__(type_arg, value, tb)

class WrongNumArguments(ValueError):
    def __init__(self, name, signature, ftype, arguments, htype):
        message = """
The wrong number of arguments has been passed to wrapped C function:
Expected {expected} arguments, received {received}
Name: {name}
C signature: {c_type}
Arguments: {arguments}
Handle type: {htype}
          """.format(
            name=name,
            c_type=signature,
            arguments=arguments,
            expected=len(ftype.args),
            received=len(arguments),
            htype=htype)
        super(WrongNumArguments, self).__init__(message)


class InvalidArguments(ValueError):
    def __init__(self, name, signature, arguments, err_msg):
        message = """
Invalid arguments passed to wrapped C function:
cffi error: {err_msg}
Name: {name}
C signature: {c_type}
Arguments: {arguments}
          """.format(
            name=name,
            c_type=signature,
            err_msg=err_msg,
            arguments=arguments)
        super(InvalidArguments, self).__init__(message)


class FunctionWrapper(object):
    def __init__(self, fun, name, t, ffi, add_handle=False):
        self.arg_trans = []
        self.fun = fun
        self.add_handle = add_handle
        self.build_argument_translation_list(t)
        self.trans_len = len(self.arg_trans)
        self.is_error = lambda x: False
        self.function_type = t
        self.name = name
        self.ffi = ffi
        if t.result.kind in ('pointer', 'array'):
            self.is_error = lambda x: x is None
        elif t.result in [ffi.typeof(x)
                          for x in ('char', 'int', 'short', 'long',
                                    'long long', 'int32_t', 'int64_t')]:
            self.is_error = lambda x: x < 0

    def set_error_check(self, fun):
      self.is_error = fun

    def build_argument_translation_list(self, t):
        alist = t.args[1:] if self.add_handle else t.args
        for i, a in enumerate(alist, start=1 if self.add_handle else 0):
            if a.kind == 'array' or a.kind == 'pointer':
                self.arg_trans.append(i)

    def __call__(self, calling_object, *args_in):
        # print holder.__name__, 'got', calling_object, args_in
        calling_object.ffi.errno = 0
        if self.trans_len == 0:
            try:
                if self.add_handle:
                    result = self.fun(calling_object.handle, *args_in)
                else:
                    result = self.fun(*args_in)
            except TypeError as te:
                raise InvalidArguments(self.name, self.ffi.getctype(
                    self.function_type), args, te.message)
        else:
            args = []
            if self.add_handle:
                args.append(calling_object.handle)
            args.extend(args_in)
            if len(self.function_type.args) != len(args):
                raise WrongNumArguments(self.name,
                                        self.ffi.getctype(self.function_type),
                                        self.function_type, args,
                                        self.ffi.typeof(calling_object.handle) if calling_object.handle else "None")
            for i in self.arg_trans:
                if args[i] is None:
                    args[i] = calling_object.ffi.NULL
                elif isinstance(args[i], WrapperBase):
                    # Unpack wrapper objects
                    args[i] = args[i].handle
                elif isinstance(args[i], unicode):
                    # convert unicode string to ascii to make cffi happy
                    args[i] = str(args[i])

            try:
                result = self.fun(*args)
            except TypeError as te:
                raise InvalidArguments(self.name, self.ffi.getctype(
                    self.function_type), args, te.message)

        if result == calling_object.ffi.NULL:
            result = None

        # Convert errno errors into python exceptions

        if self.is_error(result):
            err = calling_object.ffi.errno
            if err != 0:
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
                 prefixes=[],
                 destructor=None, ):
        self._handle = None
        super(Wrapper, self).__init__()

        self.ffi = ffi
        self.lib = lib
        self._handle = handle
        self.match = match
        self.filter_match = filter_match
        self.prefixes = prefixes
        self.destructor = destructor

        self.NULL = ffi.NULL
        self.initialized = True

    def check_handle(self, name, t):
        if self.match is not None and self._handle is not None:
            if t.kind == 'function' and t.args[
                0
            ] == self.match:  #first argument is of handle type
                return True
            else:
                if self.filter_match:
                    raise AttributeError(
                        "Flux Wrapper object {} masks function {} type: {} match: {}".format(
                            self, name, t, self.match))
        return False

    def check_wrap(self, fun, name):
        t = self.ffi.typeof(fun)

        return FunctionWrapper(fun, name, t, self.ffi,
                               add_handle=self.check_handle(name, t))

    def __getattr__(self, name):
        fun = None
        llib = self.__getattribute__("lib")
        if re.match('__.*__', name):
          # This is a python internal name, skip it
          raise AttributeError
        #try it bare
        try:
            fun = getattr(llib, name)
        except AttributeError:
            for prefix in self.prefixes:
                try:
                    #wrap it
                    fun = getattr(llib, prefix + name)
                    break
                except AttributeError:
                    pass
        if fun is None:
            # Return a proxy class to generate a good error on call
            setattr(self.__class__, name, ErrorPrinter(name, self.prefixes))
            return self.__getattribute__(name)

        if not callable(fun): # pragma: no cover
          return fun

        new_fun = self.check_wrap(fun, name)
        new_method = MethodType(new_fun, None, self.__class__)
        # Store the wrapper function into the class to prevent a second lookup
        setattr(self.__class__, name, new_method)
        return self.__getattribute__(name)

    def __clear(self):
        # avoid recursion
        if not self.initialized:
            return
        h = self._handle
        if h is None:
            return
        if self.destructor is None:
            return
        self.destructor(h)
        # ensure we don't double destruct
        self._handle = None

    @property
    def handle(self):
        return self._handle

    @handle.setter
    def handle(self, h):
        """ Override handle setter to clean up old handle if requested """
        if h is not None and self.match is not None:
            if self.ffi.typeof(h) != self.match:
                raise TypeError("Invalid handle {} of type {} assigned to "
                                "wrapper with handle type {}".format(
                                    h, self.ffi.typeof(h), self.match
                                    ))
        if self._handle is not None:
            self.__clear()
        self._handle = h

    def __del__(self):
        self.__clear()

    def __enter__(self):
        return self

    def __exit__(self, type_arg, value, tb):
        self.__clear()
