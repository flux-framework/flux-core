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
from pathlib import PurePath

import flux
from flux.job import JobID, job_list_id
from flux.uri import JobURI, URIResolverPlugin, URIResolverURI


def filter_slash(iterable):
    return list(filter(lambda x: "/" not in x, iterable))


class URIResolver(URIResolverPlugin):
    """A URI resolver that attempts to fetch the remote_uri for a job"""

    def describe(self):
        return "Get URI for a given Flux JOBID"

    def _do_resolve(self, uri, flux_handle, force_local=False):
        #
        #  Convert a possible hierarchy of jobids to a list, dropping any
        #   extraneous '/' (e.g. //id0/id1 -> [ "id0", "id1" ]
        jobids = filter_slash(PurePath(uri.path).parts)

        #  Pop the first jobid off the list, this id should be local:
        arg = jobids.pop(0)
        try:
            jobid = JobID(arg)
        except OSError as exc:
            raise ValueError(f"{arg} is not a valid jobid")

        #  Fetch the jobinfo object for this job
        try:
            job = job_list_id(
                flux_handle, jobid, attrs=["state", "annotations"]
            ).get_jobinfo()
            if job.state != "RUN":
                raise ValueError(f"jobid {arg} is not running")
            uri = job.user.uri
        except FileNotFoundError as exc:
            raise ValueError(f"jobid {arg} not found") from exc

        if str(uri) == "":
            raise ValueError(f"URI not found for job {arg}")

        #  If there are more jobids in the hierarchy to resolve, resolve
        #   them recursively
        if jobids:
            arg = "/".join(jobids)
            resolver_uri = URIResolverURI(f"jobid:{arg}")
            if force_local:
                uri = JobURI(uri).local
            return self._do_resolve(
                resolver_uri, flux.Flux(uri), force_local=force_local
            )
        return uri

    def resolve(self, uri):
        force_local = False
        if "local" in uri.query_dict or "FLUX_URI_RESOLVE_LOCAL" in os.environ:
            force_local = True
        return self._do_resolve(uri, flux.Flux(), force_local=force_local)
