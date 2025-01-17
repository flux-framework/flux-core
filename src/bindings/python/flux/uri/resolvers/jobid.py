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
from pathlib import PurePosixPath

import flux
from flux.job import JobID, job_list_id
from flux.uri import JobURI, URIResolverPlugin, URIResolverURI


def wait_for_uri(flux_handle, jobid):
    """Wait for memo event containing job uri, O/w finish event"""
    for event in flux.job.event_watch(flux_handle, jobid):
        if event.name == "memo" and "uri" in event.context:
            return event.context["uri"]
        if event.name == "finish":
            return None
    return None


def resolve_parent(handle):
    """Return parent-uri if instance-level > 0, else local-uri"""
    if int(handle.attr_get("instance-level")) > 0:
        return handle.attr_get("parent-uri")
    return handle.attr_get("local-uri")


def resolve_root(flux_handle):
    """Return the URI of the top-level, or root, instance."""
    handle = flux_handle
    while int(handle.attr_get("instance-level")) > 0:
        handle = flux.Flux(resolve_parent(handle))
    return handle.attr_get("local-uri")


def resolve_jobid(flux_handle, arg, wait):
    try:
        jobid = JobID(arg)
    except OSError as exc:
        raise ValueError(f"{arg} is not a valid jobid")

    try:
        if wait:
            uri = wait_for_uri(flux_handle, jobid)
        else:
            #  Fetch the jobinfo object for this job
            job = job_list_id(
                flux_handle, jobid, attrs=["state", "annotations"]
            ).get_jobinfo()
            if job.state != "RUN":
                raise ValueError(f"jobid {arg} is not running")
            uri = job.user.uri
    except FileNotFoundError as exc:
        raise ValueError(f"jobid {arg} not found") from exc

    if uri is None or str(uri) == "":
        raise ValueError(f"URI not found for job {arg}")
    return uri


class URIResolver(URIResolverPlugin):
    """A URI resolver that attempts to fetch the remote_uri for a job"""

    def describe(self):
        return "Get URI for a given Flux JOBID"

    def _do_resolve(
        self, uri, flux_handle, force_local=False, wait=False, hostname=None
    ):
        #
        #  Convert a possible hierarchy of jobids to a list
        jobids = list(PurePosixPath(uri.path).parts)

        #  If path is empty, return current enclosing URI
        if not jobids:
            return flux_handle.attr_get("local-uri")

        #  Pop the first jobid off the list, if a jobid it should be local,
        #  otherwise "/" for the root URI or ".." for parent URI:
        arg = jobids.pop(0)
        if arg == "/":
            uri = resolve_root(flux_handle)
        elif arg == "..":
            uri = resolve_parent(flux_handle)
            #  Relative paths always use a local:// uri. But, if a jobid was
            #  resolved earlier in the path, then use the hostname associated
            #  with that job.
            if hostname:
                uri = JobURI(uri, remote_hostname=hostname).remote
        else:
            uri = resolve_jobid(flux_handle, arg, wait)
            hostname = JobURI(uri).netloc

        #  If there are more jobids in the hierarchy to resolve, resolve
        #   them recursively
        if jobids:
            arg = "/".join(jobids)
            resolver_uri = URIResolverURI(f"jobid:{arg}")
            if force_local:
                uri = JobURI(uri).local
            return self._do_resolve(
                resolver_uri,
                flux.Flux(uri),
                force_local=force_local,
                hostname=hostname,
            )
        return uri

    def resolve(self, uri):
        force_local = False
        wait = False
        if "local" in uri.query_dict or "FLUX_URI_RESOLVE_LOCAL" in os.environ:
            force_local = True
        if "wait" in uri.query_dict:
            wait = True
        return self._do_resolve(uri, flux.Flux(), force_local=force_local, wait=wait)
