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
from collections.abc import Mapping

from flux.hostlist import Hostlist
from flux.idset import IDset
from flux.resource import Rlist
from flux.resource.ResourceSetImplementation import ResourceSetImplementation


# pylint: disable=too-many-public-methods
class ResourceSet:
    """
    ResourceSet object constructor.

    :param arg: Argument from which to construct a ResourceSet. `arg`
                may be a serialized R string, a decoded Mapping of
                an R string, or a valid ResourceSet implementation
                (an instance of ResourceSetImplementation)

    :param version: R specification version

    :raises TypeError: A ResourceSet cannot be instantiated from arg
    :raises ValueError: Invalid R version, or invalid R encoding
    :raises KeyError: arg was a dict without a 'version' key
    :raises json.decoder.JSONDecodeError: `arg` is an Invalid JSON string

    All parameters are optional. ResourceSet() will initialize an
    empty, version 1 ResourceSet object.
    """

    def __init__(self, arg=None, version=1):
        self._state = None

        if isinstance(arg, ResourceSetImplementation):
            #  If argument is a resource set implementation, instantiate
            #   from that and return immediately
            self.impl = arg
            self.version = arg.version
            return

        if isinstance(arg, str):
            #  If arg is a string, assume an encoded R representation.
            #  decode to a mapping:
            arg = json.loads(arg)

        if isinstance(arg, Mapping):
            #  If argument is a mapping, grab version field for later use
            version = arg["version"]
        elif arg is not None:
            # arg must be ResourceSetImplementation, Mapping, string or None:
            tstr = type(arg)
            raise TypeError(f"ResourceSet cannot be instantiated from {tstr}")

        #  Instantiate implementation from version
        #  note: only version 1 supported for now
        if version == 1:
            self.version = 1
            self.impl = Rlist(arg)
        else:
            raise ValueError(f"R version {version} not supported")

    def __str__(self):
        return self.dumps()

    def __and__(self, arg):
        return self.intersect(arg)

    def __sub__(self, arg):
        return self.diff(arg)

    def __or__(self, arg):
        return self.union(arg)

    def dumps(self):
        """Return a short-form, human-readable string of a ResourceSet object"""
        return self.impl.dumps()

    def encode(self):
        """Encode a ResourceSet object to its serialized string representation"""
        return self.impl.encode()

    def count(self, name):
        """
        Return a count of resource objects within a ResourceSet

        :param name: The name of the object to count, e.g. "core"
        """
        return self.impl.count(name)

    def append(self, *args):
        """Append a ResourceSet to another"""
        for arg in args:
            if not isinstance(arg, ResourceSet):
                arg = ResourceSet(arg, version=self.version)
            self.impl.append(arg.impl)
        return self

    def add(self, *args):
        """
        Add resources to a ResourceSet that are not already members
        """
        for arg in args:
            if not isinstance(arg, ResourceSet):
                arg = ResourceSet(arg, version=self.version)
            self.impl.add(arg.impl)
        return self

    def copy(self):
        """Return a copy of a ResourceSet"""
        rset = ResourceSet(self.impl.copy())
        rset.state = self.state
        return rset

    def _run_op(self, method, *args):
        result = self.copy()
        for arg in args:
            if not isinstance(arg, ResourceSet):
                arg = ResourceSet(arg, version=self.version)
            impl = getattr(result.impl, method)(arg.impl)
            result = ResourceSet(impl)
        result.state = self.state
        return result

    def union(self, *args):
        """
        Return a new ResourceSet with elements from this set and all others.

        Equivalent to ``set | other | ...``.
        """
        return self._run_op("union", *args)

    def diff(self, *args):
        """
        Return a new ResourceSet with elements in this set that are not in the others.

        Equivalent to ``set - other - ...``.
        """
        return self._run_op("diff", *args)

    def intersect(self, *args):
        """
        Return a new ResourceSet with elements common to this set and all others.

        Equivalent to ``set & other & ...``.
        """
        return self._run_op("intersect", *args)

    def copy_constraint(self, constraint):
        """
        Return a copy of a ResourceSet containing only those resources that
        match the RFC 31 constraint object `constraint`

        :param constraint: An RFC 31 constraint object in encoded string
                           form or as Python mapping. (The mapping will be
                           converted to a JSON string)
        """
        return ResourceSet(self.impl.copy_constraint(constraint))

    def set_property(self, name, ranks=None):
        """
        Set property 'name' on optional 'ranks' (all ranks if ranks is None)
        """
        if ranks is None:
            ranks = str(self.ranks)
        self.impl.set_property(name, ranks)
        return self

    def get_properties(self):
        """
        Return an RFC 20 properties object for this ResourceSet
        """
        return self.impl.get_properties()

    def remove_ranks(self, ranks):
        """
        Remove the rank or ranks specified from the ResourceSet

        :param ranks: A flux.idset.IDset object, or number or string which
                      can be converted into an IDset, containing the ranks
                      to remove
        """
        if not isinstance(ranks, IDset):
            ranks = IDset(ranks)
        self.impl.remove_ranks(ranks)
        return self

    def copy_ranks(self, ranks):
        """
        Copy only the rank or ranks specified from the ResourceSet

        :param ranks: A flux.idset.IDset object, or number or string which
                      can be converted into an IDset, containing the ranks
                      to copy
        """
        if not isinstance(ranks, IDset):
            ranks = IDset(ranks)
        rset = ResourceSet(self.impl.copy_ranks(ranks))

        #  Preserve current state
        rset.state = self.state
        return rset

    def host_ranks(self, hosts, ignore_nomatch=False):
        """
        Translate a set of hostnames to broker ranks using the current
        ResourceSet.

        Args:
            ignore_nomatch (bool): If True, then hosts that are not in
                the current ResourceSet are ignored, and only matching
                hosts result in a returned rank. O/w, FileNotFound error
                 is raised.
        Returns:
            list of rank ids in order of provided hosts
        """
        if not isinstance(hosts, Hostlist):
            hosts = Hostlist(hosts)
        ranks = list(self.ranks)
        index = self.nodelist.index(hosts, ignore_nomatch=ignore_nomatch)
        return [ranks[i] for i in index]

    @property
    def nodelist(self):
        """
        Return a flux.hostlist.Hostlist containing the list of hosts in
        this ResourceSet
        """
        return self.impl.nodelist()

    @property
    def state(self):
        """An optional state associated with this ResourceSet (e.g. "up")"""
        return self._state

    @state.setter
    def state(self, value):
        """Set an optional state for this ResourceSet"""
        self._state = value

    @property
    def ranks(self):
        """
        Return a flux.idset.IDset containing the set of ranks in this
        ResourceSet
        """
        return self.impl.ranks()

    @property
    def nnodes(self):
        return self.impl.nnodes()

    @property
    def ncores(self):
        return self.impl.count("core")

    @property
    def ngpus(self):
        return self.impl.count("gpu")

    @property
    def rlist(self):
        return self.impl.dumps()

    @property
    def properties(self):
        return ",".join(json.loads(self.get_properties()).keys())
