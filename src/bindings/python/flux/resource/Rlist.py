###############################################################
# Copyright 2020 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import json
import socket
from collections.abc import Mapping

from _flux._rlist import ffi, lib
from flux.hostlist import Hostlist
from flux.idset import IDset
from flux.resource.ResourceSetImplementation import ResourceSetImplementation
from flux.wrapper import Wrapper, WrapperPimpl


@ResourceSetImplementation.register
class Rlist(WrapperPimpl):
    version = 1

    class InnerWrapper(Wrapper):
        def __init__(self, rstring=None, handle=None):

            if handle is None:
                if rstring is None:
                    handle = lib.rlist_create()
                else:
                    if isinstance(rstring, Mapping):
                        rstring = json.dumps(rstring)
                    handle = lib.rlist_from_R(rstring.encode("utf-8"))
            if handle == ffi.NULL:
                raise ValueError(f"Rlist: invalid argument")
            super().__init__(
                ffi,
                lib,
                match=ffi.typeof("struct rlist *"),
                prefixes=["rlist_"],
                destructor=lib.rlist_destroy,
                handle=handle,
            )

    def __init__(self, rstring=None, handle=None):
        super().__init__()
        self.pimpl = self.InnerWrapper(rstring, handle)

    def dumps(self):
        val = lib.rlist_dumps(self.handle)
        result = ffi.string(val).decode("utf-8")
        lib.free(val)
        return result

    def encode(self):
        val = lib.rlist_encode(self.handle)
        result = ffi.string(val).decode("utf-8")
        lib.free(val)
        return result

    def nodelist(self):
        return Hostlist(handle=self.pimpl.nodelist())

    def get_properties(self):
        val = lib.rlist_properties_encode(self.handle)
        result = ffi.string(val).decode("utf-8")
        lib.free(val)
        return result

    def ranks(self, hosts=None):
        if hosts is None:
            return IDset(handle=self.pimpl.ranks())
        return IDset(handle=self.pimpl.hosts_to_ranks(hosts))

    def nnodes(self):
        return self.pimpl.nnodes()

    def count(self, name):
        return self.pimpl.count(name)

    def remap(self):
        self.pimpl.remap()
        return self

    def append(self, arg):
        self.pimpl.append(arg)
        return self

    def add(self, arg):
        self.pimpl.add(arg)
        return self

    def copy(self):
        return Rlist(handle=self.pimpl.copy_empty())

    def union(self, arg):
        return Rlist(handle=self.pimpl.union(arg))

    def intersect(self, arg):
        return Rlist(handle=self.pimpl.intersect(arg))

    def diff(self, arg):
        return Rlist(handle=self.pimpl.diff(arg))

    def add_rank(self, rank, hostname=None, cores="0"):
        if hostname is None:
            hostname = socket.gethostname()
        self.pimpl.append_rank_cores(hostname, rank, cores)
        return self

    def remove_ranks(self, ranks):
        if not isinstance(ranks, IDset):
            ranks = IDset(ranks)
        self.pimpl.remove_ranks(ranks)
        return self

    def copy_ranks(self, ranks):
        return Rlist(handle=self.pimpl.copy_ranks(ranks))

    def add_child(self, rank, name, ids):
        self.pimpl.rank_add_child(rank, name, ids)
        return self

    def set_property(self, name, ranks):
        error = ffi.new("flux_error_t *")
        try:
            self.pimpl.add_property(error, name, ranks)
        except OSError as exc:
            raise ValueError(
                "set_property: " + ffi.string(error.text).decode("utf-8")
            ) from exc

    def copy_constraint(self, constraint):
        error = ffi.new("flux_error_t *")
        if not isinstance(constraint, str):
            constraint = json.dumps(constraint)
        try:
            handle = self.pimpl.copy_constraint_string(constraint, error)
        except OSError as exc:
            raise ValueError(
                "copy_constraint: " + ffi.string(error.text).decode("utf-8")
            ) from exc
        return Rlist(handle=handle)
