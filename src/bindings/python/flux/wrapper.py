"""
Flux interface wrapper generator.
This could, in principle, be used for other projects as well, but it encodes a
number of assumptions about the error propagation and handling that flux uses.
"""
import re
import os
import inspect
import six


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

    def __init__(self):
        self.pimpl = None
        super(WrapperPimpl, self).__init__()

    @property
    def handle(self):
        return self.pimpl.handle

    @handle.setter
    def handle(self, handle):
        self.pimpl.handle = handle

    def __enter__(self):
        return self

    def __exit__(self, type_arg, value, last):
        self.pimpl.__exit__(type_arg, value, last)


class WrongNumArguments(ValueError):

    def __init__(self, name, signature, ftype, arguments, htype):
        message = """
The wrong number of arguments has been passed to wrapped C function:
Expected {expected} arguments, received {received}
Name: {name}
C signature: {c_type}
Arguments: {arguments}
Handle type: {htype}
          """.format(name=name,
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
          """.format(name=name,
                     c_type=signature,
                     err_msg=err_msg,
                     arguments=arguments)
        super(InvalidArguments, self).__init__(message)


class FunctionWrapper(object):

    def __init__(self, fun, name, function_type, ffi, add_handle=False):
        self.arg_trans = []
        self.fun = fun
        self.add_handle = add_handle
        self.build_argument_translation_list(function_type)
        self.is_error = lambda x: False
        self.function_type = function_type
        self.name = name
        self.ffi = ffi
        if function_type.result.kind in ('pointer', 'array'):
            self.is_error = lambda x: x is None
        elif function_type.result in [ffi.typeof(x)
                                      for x in ('char', 'int', 'short',
                                                'long', 'long long', 'int32_t',
                                                'int64_t')]:
            self.is_error = lambda x: x < 0

    def set_error_check(self, fun):
        self.is_error = fun

    def build_argument_translation_list(self, fun_type):
        alist = fun_type.args[1:] if self.add_handle else fun_type.args
        for i, arg in enumerate(alist, start=1 if self.add_handle else 0):
            if arg.kind == 'array' or arg.kind == 'pointer':
                self.arg_trans.append(i)

    def __call__(self, calling_object, *args_in):
        calling_object.ffi.errno = 0
        caller = calling_object.handle
        args = [caller, ] + \
            list(args_in) if self.add_handle else list(args_in)

        if len(self.function_type.args) != len(args):
            calling_type = self.ffi.typeof(caller) if caller else "None"
            raise WrongNumArguments(self.name,
                                    self.ffi.getctype(self.function_type),
                                    self.function_type, args,
                                    calling_type)
        for i in self.arg_trans:
            if args[i] is None:
                args[i] = calling_object.ffi.NULL
            elif isinstance(args[i], WrapperBase):
                # Unpack wrapper objects
                args[i] = args[i].handle
            elif isinstance(args[i], six.text_type):
                # convert unicode string to ascii to make cffi happy
                args[i] = str(args[i])

        try:
            result = self.fun(*args)
        except TypeError as err:
            raise InvalidArguments(self.name, self.ffi.getctype(
                self.function_type), args_in, err.message)

        if result == calling_object.ffi.NULL:
            result = None

        elif result is not None and calling_object.ffi.typeof(result) == "char *":
            result = calling_object.ffi.string(result)

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
                 prefixes=tuple(),
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

    def check_handle(self, name, fun_type):
        if self.match is not None and self._handle is not None:
            if (fun_type.kind == 'function' and
                    fun_type.args[0] == self.match):
                # first argument is of handle type
                return True
            else:
                if self.filter_match:
                    raise AttributeError(
                        "Flux Wrapper object " + str(self) +
                        "masks function {} type: {} match: {}".format(
                            name, fun_type, self.match))
        return False

    def check_wrap(self, fun, name):
        fun_type = self.ffi.typeof(fun)

        return FunctionWrapper(fun, name, fun_type, self.ffi,
                               add_handle=self.check_handle(name, fun_type))

    def __getattr__(self, name):
        fun = None
        llib = self.__getattribute__("lib")
        if re.match('__.*__', name):
            # This is a python internal name, skip it
            raise AttributeError
        # try it bare
        try:
            fun = getattr(llib, name)
        except AttributeError:
            for prefix in self.prefixes:
                try:
                    # wrap it
                    fun = getattr(llib, prefix + name)
                    break
                except AttributeError:
                    pass
        if fun is None:
            # Return a proxy class to generate a good error on call
            setattr(self.__class__, name, ErrorPrinter(name, self.prefixes))
            return self.__getattribute__(name)

        if not callable(fun):  # pragma: no cover
            return fun

        new_fun = self.check_wrap(fun, name)
        new_method = six.create_bound_method(new_fun, self)
        # Store the wrapper function into the class to prevent a second lookup
        setattr(self.__class__, name, new_method)
        return self.__getattribute__(name)

    def __clear(self):
        # avoid recursion
        handle = self._handle
        if handle is None:
            return
        if self.destructor is None:
            return
        self.destructor(handle)
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

    def __exit__(self, type_arg, value, unused):
        self.__clear()
