###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import json
import numbers

from _flux._resourcecount import ffi, lib
from flux.wrapper import Wrapper, WrapperPimpl

RESOURCECOUNT_MAX = lib.RESOURCECOUNT_MAX
RESOURCECOUNT_INVALID_VALUE = lib.RESOURCECOUNT_INVALID_VALUE
RESOURCECOUNT_FLAG_SHORT = lib.RESOURCECOUNT_FLAG_SHORT
RESOURCECOUNT_FLAG_BRACKETS = lib.RESOURCECOUNT_FLAG_BRACKETS


class ResourceCountIterator:
    def __init__(self, count):
        self._count = count
        self._next = count.first

    def __iter__(self):
        return self

    def __next__(self):
        if self._next == RESOURCECOUNT_INVALID_VALUE * self._count.scale_factor:
            raise StopIteration
        result = self._next
        self._next = self._count.next(result)
        return result


class ResourceCount(WrapperPimpl):
    """A Flux jobspec resource count

    The ResourceCount class wraps libresourcecount, and represents the count
    key of a jobspec resource object. A resource count is one of a simple
    integer, an RFC 14 range, or an RFC 22 idset. See resourcecount_decode(3).

    A Python ResourceCount object may be created from a string such as
    "4", "2-8", "2-8:2:*", or "1,7-9", from an integer, or from a dict
    of the form used by a JSON parsed jobspec, e.g. {"min": 2, "max": 8}.
    For example:

    >>> print(ResourceCount("4"))
    4
    >>> print(ResourceCount(4))
    4
    >>> print(ResourceCount({"min": 2, "max": 8}))
    2-8

    Iteration yields every value the resource count may take, from first() up
    to and including last(). ResourceCounts which are unbounded (e.g. "2+") iterate
    up to the last valid value before RESOURCECOUNT_INVALID_VALUE * scale_factor.

    """

    class InnerWrapper(Wrapper):
        def __init__(
            self,
            arg="",
            handle=None,
        ):
            if handle is None:
                handle = lib.resourcecount_decode(arg.encode("utf-8"))
                if handle == ffi.NULL:
                    raise ValueError(f"ResourceCount(): Invalid argument: {arg}")
            super().__init__(
                ffi,
                lib,
                match=ffi.typeof("struct resourcecount *"),
                prefixes=["resourcecount_", "RESOURCECOUNT_"],
                destructor=lib.resourcecount_destroy,
                handle=handle,
            )

    def __init__(self, arg="", flags=RESOURCECOUNT_FLAG_SHORT, handle=None):
        super().__init__()
        if isinstance(arg, ResourceCount):
            arg = str(arg)
        elif isinstance(arg, dict):
            arg = json.dumps(arg)
        elif isinstance(arg, numbers.Integral):
            arg = str(int(arg))

        self.default_flags = flags
        self.scale_factor = 1
        try:
            self.pimpl = self.InnerWrapper(arg=arg, handle=handle)
        except (TypeError, AttributeError):
            raise TypeError(
                f"ResourceCount() expected a count string, integer, or dict, got {type(arg)}"
            )

    def __str__(self):
        return self.encode()

    def __repr__(self):
        return f"ResourceCount('{self.encode()}')"

    def __iter__(self):
        return ResourceCountIterator(self)

    def set_flags(self, flags):
        """Set default flags for ResourceCount encoding:
        valid flags are RESOURCECOUNT_FLAG_SHORT and RESOURCECOUNT_FLAG_BRACKETS
        """
        self.default_flags = flags

    def encode(self, flags=None):
        """Encode a ResourceCount to a string.
        :param: flags: (optional) flags to influence encoding
        """
        if flags is None:
            flags = self.default_flags
        #
        #  N.B. Do not use automatic wrapper call here to avoid leaking
        #  `char *` result. Instead, explicitly call free() after copying
        #  the returned string to Python
        #
        val = lib.resourcecount_encode(self.handle, flags)
        if val == ffi.NULL:
            raise ValueError(f"ResourceCount.encode(): Invalid flags: {flags}")
        result = ffi.string(val)
        lib.free(val)
        return result.decode("utf-8")

    @property
    def is_integer(self):
        """True if the count is a simple integer"""
        return self.pimpl.is_integer()

    @property
    def is_unbounded(self):
        """True if the count is an unbounded range"""
        return self.pimpl.is_unbounded()

    @property
    def is_discrete(self):
        """True if the count specifies discrete values rather than a range

        The SHORT encoding of a simple count contains neither a comma nor a
        colon ("4", "2-8", "1-3", "2+"), while a count with gaps encodes as
        "1,7-9" and a stepped count as "2-8:2:*".
        """
        text = self.encode(flags=RESOURCECOUNT_FLAG_SHORT)
        return "," in text or ":" in text

    @staticmethod
    def _check_integer(i, name):
        if not isinstance(i, numbers.Integral):
            raise TypeError(f"ResourceCount.{name} supports integers, not {type(i)}")
        if i < 0:
            raise ValueError(f"negative integer passed to ResourceCount.{name}")

    @property
    def first(self):
        """Return the first (minimum) valid value that the count may take"""
        return self.pimpl.first() * self.scale_factor

    @property
    def last(self):
        """Return the last (maximum) valid value that the count may take
        Returns None for an unbounded range
        """
        return None if self.is_unbounded else self.pimpl.last() * self.scale_factor

    def next(self, i):
        """Return the next value that the count may take after value i"""
        self._check_integer(i, "next")
        return self.pimpl.next(i // self.scale_factor) * self.scale_factor

    def scale(self, factor):
        """Scale the values of the count by an integer factor"""
        self._check_integer(factor, "next")
        self.scale_factor = factor
        return self


def decode(string):
    """Decode a count string and return ResourceCount object"""
    return ResourceCount(string)
