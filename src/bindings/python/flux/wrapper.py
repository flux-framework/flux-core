###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""
Flux interface wrapper generator.
This could, in principle, be used for other projects as well, but it encodes a
number of assumptions about the error propagation and handling that flux uses.
"""
import errno
import inspect
import os
import re
from types import MethodType
from typing import Any, Dict


class MissingFunctionError(Exception):
    def __init__(self, name, c_name, name_list, arguments):

        call_stack = inspect.stack()
        caller = inspect.getframeinfo(call_stack[2][0])

        message = """
A non-existent or unavailable function invocation has been detected.
Has this function been recently removed or renamed?

Name called: {name}
Possible C names: {name_list}
Arguments: {arguments}
Likely intended C invocation: {c_name}{arguments}
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
            c_name=c_name,
        )
        # Call the base class constructor with the parameters it needs
        super(MissingFunctionError, self).__init__(message)


class ErrorPrinter(object):
    def __init__(self, name, prefixes):
        self.name = name
        self.prefixes = prefixes

    def __call__(self, *args):
        c_name = self.name
        if not any([re.search("^" + x, self.name) for x in self.prefixes]):
            c_name = self.prefixes[0] + self.name

        raise MissingFunctionError(
            self.name, c_name, [p + self.name for p in self.prefixes], args
        )


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
          """.format(
            name=name,
            c_type=signature,
            arguments=arguments,
            expected=len(ftype.args),
            received=len(arguments),
            htype=htype,
        )
        super(WrongNumArguments, self).__init__(message)


class InvalidArguments(ValueError):
    def __init__(self, name, signature, arguments):
        message = """
Invalid arguments passed to wrapped C function:
Name: {name}
C signature: {c_type}
Arguments: {arguments}
          """.format(
            name=name, c_type=signature, arguments=arguments
        )
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
        if function_type.result.kind in ("pointer", "array"):
            self.is_error = lambda x: x is None
        elif function_type.result in [
            ffi.typeof(x)
            for x in ("char", "int", "short", "long", "long long", "int32_t", "int64_t")
        ]:
            self.is_error = lambda x: x < 0

    def set_error_check(self, fun):
        self.is_error = fun

    def build_argument_translation_list(self, fun_type):
        alist = fun_type.args[1:] if self.add_handle else fun_type.args
        for i, arg in enumerate(alist, start=1 if self.add_handle else 0):
            if arg.kind == "array" or arg.kind == "pointer":
                self.arg_trans.append(i)

    def __call__(self, calling_object, *args_in):
        calling_object.ffi.errno = 0
        caller = calling_object.handle
        if self.add_handle and caller is None:
            raise ValueError(
                "Attempting to call a cached, bound method that requires a "
                "handle with a NULL handle"
            )

        args = [caller] + list(args_in) if self.add_handle else list(args_in)

        if len(self.function_type.args) != len(args):
            calling_type = self.ffi.typeof(caller) if caller else "None"
            raise WrongNumArguments(
                self.name,
                self.ffi.getctype(self.function_type),
                self.function_type,
                args,
                calling_type,
            )
        for i in self.arg_trans:
            if args[i] is None:
                args[i] = calling_object.ffi.NULL
            elif isinstance(args[i], WrapperBase):
                # Unpack wrapper objects
                args[i] = args[i].handle
            elif isinstance(args[i], str):
                args[i] = args[i].encode("utf-8", errors="surrogateescape")

        try:
            result = self.fun(*args)
        except TypeError as err:
            raise InvalidArguments(
                self.name, self.ffi.getctype(self.function_type), args_in
            ) from err

        if result == calling_object.ffi.NULL:
            result = None

        elif (
            result is not None
            and self.function_type.result is calling_object.ffi.typeof("char *")
        ):
            result = calling_object.ffi.string(result)

        # Convert errno errors into python exceptions

        if self.is_error(result):
            errnum = calling_object.ffi.errno
            if errnum != 0:
                raise EnvironmentError(
                    errnum,
                    "{}: {}".format(
                        (
                            errno.errorcode[errnum]
                            if errnum in errno.errorcode
                            else "Errno" + str(errnum)
                        ),
                        os.strerror(errnum),
                    ),
                )

        return result


SIGS_: Dict[Any, Any] = {}


class Wrapper(WrapperBase):
    """
    Forms a wrapper around an interface that dynamically searches for undefined
    names, and can detect and pass a handle argument of specified type when it
    is found in the signature of an un-specified target function.
    """

    def __init__(
        self,
        ffi,
        lib,
        handle=None,
        match=None,
        filter_match=True,
        prefixes=tuple(),
        destructor=None,
    ):
        self._handle = None
        super(Wrapper, self).__init__()

        self.ffi = ffi
        self.lib = lib
        self._handle = handle
        self.match = match
        self.filter_match = filter_match
        self.prefixes = prefixes
        self.destructor = destructor
        # this is an error-checking dance to ensure that the class-based caching of
        # callables is safe by only allowing one set of prefixes, filter-matches, etc.
        # per derived class of wrapper
        signature = (match, filter_match, prefixes)
        mytype = type(self)
        if SIGS_.get(mytype, None) is None:
            SIGS_[mytype] = signature
        else:
            assert (
                signature == SIGS_[mytype]
            ), f"""
signatures do not match, create a new subclass to change matching parameters:
{mytype}: mysig: {SIGS_[mytype]} sig:{signature}
            """

    def check_handle(self, name, fun_type):
        if self.match is not None and self._handle is not None:
            # pylint: disable=no-else-return
            if fun_type.kind == "function" and fun_type.args[0] == self.match:
                # first argument is of handle type
                return True
            else:
                if self.filter_match:
                    raise AttributeError(
                        "Flux Wrapper object "
                        + str(self)
                        + "masks function {} type: {} match: {}".format(
                            name, fun_type, self.match
                        )
                    )
        return False

    def check_wrap(self, fun, name):
        fun_type = self.ffi.typeof(fun)

        return FunctionWrapper(
            fun, name, fun_type, self.ffi, add_handle=self.check_handle(name, fun_type)
        )

    def __getattr__(self, name):
        """Look up and return attributes that are not defined on this instance.

        This function is called when code tries to read from an attribute that
        is not defined in this instance's __dict__. This function looks up
        the attribute on self.lib, and adds a variety of prefixes to the
        attribute name if necessary. If the attribute is not found on self.lib,
        an AttributeError is raised; if it is found, it is returned. However,
        if the attribute is callable, the attribute is first bound to this
        instance as a method.

        :param name: the name of the attribute to find
        :raises AttributeError: if the attribute (possibly with prefixes)
        is not found on self.lib
        """
        fun = None
        if re.match("__.*__", name):
            # This is a python internal name, skip it
            raise AttributeError
        if name == "lib":
            # If "lib" is not defined in self.__dict__, this function would
            # enter an infinite recursion if it tried to access self.lib.
            raise AttributeError

        # try it bare
        llib = self.lib
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
            error_printer = ErrorPrinter(name, self.prefixes)
            setattr(self, name, error_printer)
            return error_printer

        if not callable(fun):  # pragma: no cover
            setattr(self, name, fun)
            return fun

        new_fun = self.check_wrap(fun, name)
        new_meth = MethodType(new_fun, self)

        # wrap the class in a function so it's correctly treated as a method
        def wrap_class(self_renamed, *args, **kwargs):
            return new_fun(self_renamed, *args, **kwargs)

        # Store the wrapper function into the class
        # to prevent a second lookup
        setattr(type(self), name, wrap_class)
        return new_meth

    def _clear(self):
        # avoid recursion
        if hasattr(self, "_handle") and self._handle is not None:
            handle = self._handle
        else:
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
        """Override handle setter to clean up old handle if requested"""
        if h is not None and self.match is not None:
            if self.ffi.typeof(h) != self.match:
                raise TypeError(
                    "Invalid handle {} of type {} assigned to "
                    "wrapper with handle type {}".format(
                        h, self.ffi.typeof(h), self.match
                    )
                )
        if self._handle is not None:
            self._clear()
        self._handle = h

    def __del__(self):
        self._clear()

    def __enter__(self):
        return self

    def __exit__(self, type_arg, value, unused):
        self._clear()
