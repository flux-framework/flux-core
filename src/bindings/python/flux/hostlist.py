###############################################################
# Copyright 2020 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import collections
import numbers

from _flux._hostlist import ffi, lib
from flux.wrapper import Wrapper, WrapperPimpl


class HostlistIterator:
    def __init__(self, hostlist):
        self._hostlist = hostlist
        self._index = 0

    def __iter__(self):
        return self

    def __next__(self):
        if self._index < len(self._hostlist):
            result = self._hostlist[self._index]
            self._index += 1
            return result
        raise StopIteration


class Hostlist(WrapperPimpl):
    """A Flux hostlist object

    The Hostlist class wraps libflux-hostlist to implement a list
    of hosts which can be converted to and from the RFC 29 hostlist
    encoding.
    """

    class InnerWrapper(Wrapper):
        def __init__(
            self,
            handle=None,
        ):
            super().__init__(
                ffi,
                lib,
                handle=handle,
                match=ffi.typeof("struct hostlist *"),
                prefixes=["hostlist_"],
                destructor=lib.hostlist_destroy,
            )

    def __init__(self, arg="", handle=None):
        """
        Create a new Hostlist object from RFC 29 Hostlist string or an
        Iterable containing a set of RFC 29 Hostlist strings:

        :param arg: A string or Iterable containing one or more hosts
                    encoded in RFC 29 hostlist format

        E.g.

        >>> hl = Hostlist()
        >>> hl = Hostlist("host")
        >>> hl = Hostlist("host[0-10]")
        >>> hl = Hostlist([ "foo1", "foo2" ])

        """
        #  If no `struct hostlist *` handle passed in, then create
        #   a new handle from string argument
        if handle is None:
            if isinstance(arg, str):
                handle = lib.hostlist_decode(arg.encode("utf-8"))
                if handle == ffi.NULL:
                    raise ValueError(f"Invalid hostlist: '{arg}'")
            elif isinstance(arg, collections.abc.Iterable):
                handle = lib.hostlist_create()
                for hosts in arg:
                    if not isinstance(hosts, str):
                        typestr = type(hosts)
                        raise TypeError(
                            f"Hostlist(): expected string or Iterable, got {typestr}"
                        )
                    result = lib.hostlist_append(handle, hosts.encode("utf-8"))
                    if result < 0:
                        raise ValueError(f"Invalid hostlist: '{hosts}'")
            else:
                typestr = type(arg)
                raise TypeError(
                    f"Hostlist(): expected string or Iterable, got {typestr}"
                )
        super().__init__()
        self.pimpl = self.InnerWrapper(handle)

    def __str__(self):
        return self.encode()

    def __repr__(self):
        return f"Hostlist('{self}')"

    def __len__(self):
        return self.pimpl.count()

    def __getitem__(self, index):
        """Index and slice a hostlist

        Works like normal Python list indexing, including slices:

        >>> hl = Hostlist("foo[0-9]")
        >>> hl[0]
        'foo0'
        >>> hl[9]
        'foo9'
        >>> hl[-1]
        'foo9'
        >>> hl[8:]
        ['foo8', 'foo9']
        >>> hl[1:3]
        ['foo1', 'foo2']

        """
        if isinstance(index, numbers.Integral):
            if index < 0:
                index = len(self) + index
            if 0 <= index < len(self):
                # N.B. wrapper class already calls ffi.string() on result:
                return self.pimpl.nth(index).decode("utf-8")
            raise IndexError("Hostlist index out of range")

        if isinstance(index, slice):
            return [self[n] for n in range(len(self))[index]]

        raise TypeError("Hostlist index must be integer or slice")

    def __iter__(self):
        """Return a Hostlist iterator"""
        return HostlistIterator(self)

    def __contains__(self, name):
        """Test if a hostname is in a Hostlist"""
        try:
            self.pimpl.find(name)
        except FileNotFoundError:
            return False
        return True

    def encode(self):
        """Encode a Hostlist to an RFC 29 hostlist string"""
        #
        #  N.B. Do not use automatic wrapper call here to avoid leaking
        #   `char *` result. Instead explicitly call free() after copying
        #   the returned string to Python
        #
        val = lib.hostlist_encode(self.handle)
        result = ffi.string(val)
        lib.free(val)
        return result.decode("utf-8")

    def count(self):
        """Return the number of hosts in Hostlist"""
        return self.pimpl.count()

    def append(self, *args):
        """Append one or more arguments to a Hostlist

        Args may be either a Hostlist or any valid argument to Hostlist()
        """
        count = 0
        for arg in args:
            if not isinstance(arg, Hostlist):
                arg = Hostlist(arg)
            count += self.pimpl.append_list(arg)
        return count

    def delete(self, hosts):
        """Delete host or hosts from Hostlist

        param: hosts: A Hostlist or string in RFC 29 hostlist encoding
        """
        return self.pimpl.delete(str(hosts))

    def sort(self):
        """Sort a Hostlist"""
        self.pimpl.sort()
        return self

    def uniq(self):
        """Sort and remove duplicate hostnames from Hostlist"""
        self.pimpl.uniq()
        return self

    def expand(self):
        """Convert a Hostlist to a Python list"""
        return self[:]

    def copy(self):
        """Copy a Hostlist object"""
        return Hostlist(handle=self.pimpl.copy())


def decode(arg):
    """
    Decode a string or iterable of strings in RFC 29 hostlist format
    to a Hostlist object
    """
    return Hostlist(arg)
