###############################################################
# Copyright 2021 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import os
import platform
import re
from abc import ABC, abstractmethod
from urllib.parse import parse_qs, urlparse

from flux.importer import import_plugins


class URI:
    """Simple URI class

    Parse URI strings with urllib.parse.urlparse, but with mutable properties

    Attributes:
      uri: copy of original uri argument
      scheme: addressing scheme
      netloc: network location part
      path: path part
      params: parameters if present
      query:  query component
      query_dict: query component as dictionary
      fragment: fragment identifier
    """

    def __init__(self, uri):

        self.uri = uri
        self.query_dict = {}
        uri = urlparse(self.uri)

        for name, value in uri._asdict().items():
            setattr(self, name, value)

        if self.query:
            self.query_dict = parse_qs(self.query, keep_blank_values=True)


class JobURI(URI):
    """A Flux job/instance URI

    A URI specific to a Flux instance. Same attributes as flux.uri.URI,
    with additional attributes to convert to a ``remote`` or ``local``
    URI.

    Attributes:
      remote: If local URI, returns a remote URI substituting current hostname.
              If a remote URI, returns the URI.
      local: If a remote URI, convert to a local URI. Otherwise return the URI.
    """

    force_local = os.environ.get("FLUX_URI_RESOLVE_LOCAL", False)

    def __init__(self, uri):
        super().__init__(uri)
        if self.scheme == "":
            raise ValueError(f"JobURI '{uri}' does not have a valid scheme")
        self.path = re.sub("/+", "/", self.path)
        self.remote_uri = None
        self.local_uri = None

    @property
    def remote(self):
        if not self.remote_uri:
            if self.scheme == "ssh":
                self.remote_uri = self.uri
            elif self.scheme == "local":
                hostname = platform.uname()[1]
                self.remote_uri = f"ssh://{hostname}{self.path}"
            else:
                raise ValueError(
                    f"Cannot convert JobURI with scheme {self.scheme} to remote"
                )
        return self.remote_uri

    @property
    def local(self):
        if not self.local_uri:
            if self.scheme == "local":
                self.local_uri = self.uri
            elif self.scheme == "ssh":
                self.local_uri = f"local://{self.path}"
            else:
                raise ValueError(
                    f"Cannot convert JobURI with scheme {self.scheme} to local"
                )

        return self.local_uri

    def __str__(self):
        if self.force_local:
            return self.local
        return self.uri


class URIResolverURI(URI):
    """A URI which resolves to a JobURI

    A URI used with ``FluxURIResolver.resolve``.
    Includes a workaround for ``urllib.parse.urlparse`` problems parsing
    path components with only digits.
    """

    def __init__(self, uri):
        #  Replace : with :FXX to allow path to be digits only
        super().__init__(uri.replace(":", ":FXX", 1))
        self.path = self.path.replace("FXX", "", 1)


class URIResolverPlugin(ABC):  # pragma: no cover
    """Abstract type for a plugin used to resolve Flux URIs"""

    def __init__(self, *args):
        """Initialize a URI resolver plugin"""

    @abstractmethod
    def describe(self):
        """Return a short description of the support URI scheme"""
        return NotImplementedError

    @abstractmethod
    def resolve(self, uri):
        """Resolve a get-uri URI into a FluxJobURI"""
        return NotImplementedError


class FluxURIResolver:
    """A plugin-based Flux job URI resolver class

    A FluxURIResolver loads plugins which implement different _schemes_
    for resolution simple URIs to Flux job URIs. Plugins or "resolvers"
    are loaded from the ``flux.uri.resolvers`` namespace.
    """

    def __init__(self, pluginpath=None):
        if pluginpath is None:
            pluginpath = []
        self.plugin_namespace = "flux.uri.resolvers"
        self.resolvers = {}

        plugins = import_plugins(self.plugin_namespace, pluginpath)
        if plugins:
            for scheme, plugin in plugins.items():
                self.resolvers[scheme] = plugin.URIResolver()

    def resolve(self, uri):
        """Resolve ``uri`` to a Flux job URI using dynamically loaded plugins

        The _scheme_ of the provided target URI determines which plugin
        is used to satisfy the query.

        If no _scheme_ is provided, then a default scheme of ``jobid``
        is assumed.

        URI "query" parameters may be supported by the underlying resolver
        plugin, but the ``remote`` and ``local`` query strings are always
        supported and will result in forced conversion of the returned
        job URI to a remote or local form, respectively.

        Raises NotImplementedError if no plugin for _scheme_ can be found.

        Raises ValueError if ``uri`` cannot otherwise be converted to a
        job uri by the plugin for _scheme_.

        """
        scheme = URIResolverURI(uri).scheme
        if str(scheme) == "":
            scheme = "jobid"
            uri = f"jobid:{uri}"
        if scheme in ["ssh", "local"]:
            return JobURI(uri)
        if scheme not in self.resolvers:
            raise NotImplementedError(f"No plugin for URI scheme {scheme}")

        resolver_uri = URIResolverURI(uri)

        query = resolver_uri.query_dict
        if "local" in query and "remote" in query:
            raise ValueError("Only one of 'local' or 'remote' is allowed")

        result = JobURI(self.resolvers[scheme].resolve(resolver_uri))

        #  Special case for 'local' or 'remote' query parameters:
        if "local" in query:
            result = JobURI(result.local)
        elif "remote" in query:
            result = JobURI(result.remote)
        return result

    def plugins(self):
        """Get a dict of loaded URI resolver plugins by {name: description}"""

        return {name: x.describe() for name, x in self.resolvers.items()}
