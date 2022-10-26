###############################################################
# Copyright 2020 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

from abc import ABC, abstractmethod


class ResourceSetImplementation(ABC):  # pragma: no cover
    """
    This abstract class defines the interface that a ResourceSet
    implementation shall provide in order to work with the ResourceSet
    class
    """

    @abstractmethod
    def dumps(self):
        """Return a short-form string representation of a resource set"""
        raise NotImplementedError

    def encode(self):
        """Return a JSON string representation of the resource set"""
        raise NotImplementedError

    @abstractmethod
    def nodelist(self):
        """Return the list of nodes in the resource set as a Hostlist"""
        raise NotImplementedError

    @abstractmethod
    def ranks(self):
        """Return the set of ranks in the resource set as an IDset"""
        raise NotImplementedError

    @abstractmethod
    def get_properties(self):
        """Return an RFC 20 properties object for this resource set"""
        raise NotImplementedError

    @abstractmethod
    def nnodes(self):
        """Return the number of nodes in the resource set as an IDset"""
        raise NotImplementedError

    @abstractmethod
    def count(self, name):
        """Return the total number of resources of type 'name'"""
        raise NotImplementedError

    @abstractmethod
    def copy(self):
        """Return a copy of the resource set"""
        raise NotImplementedError

    @abstractmethod
    def copy_ranks(self, ranks):
        """Return a copy of resource set with only 'ranks' included"""

    @abstractmethod
    def union(self, rset):
        """Return the union of two resource sets"""
        raise NotImplementedError

    @abstractmethod
    def intersect(self, rset):
        """Return the set intersection of two resource sets"""
        raise NotImplementedError

    @abstractmethod
    def diff(self, rset):
        """Return the set difference of two resource sets"""
        raise NotImplementedError

    @abstractmethod
    def append(self, rset):
        """Append one resource set to another"""
        raise NotImplementedError

    @abstractmethod
    def add(self, rset):
        """Add resources not existing in one set to the other"""
        raise NotImplementedError

    @abstractmethod
    def remove_ranks(self, ranks):
        """Remove an IDset of ranks from a resource set"""
        raise NotImplementedError
