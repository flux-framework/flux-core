##############################################################
# Copyright 2022 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

"""Translate dependency.name into a job id for dependency.afterok

"""

from flux.job.frobnicator import FrobnicatorPlugin


class DependencyAdder:
    """Get the system attribute for dependency.name and add a
    task dependency. Raise an error if we can't find it.
    """

    def __init__(self, config={}):
        # We don't need this for anything, just saving
        # for the heck
        self.config = config

    def add_dependency(self, jobspec):
        """We need to translate a dependency name into a job id. We will:
        1. Get the desired name from system attribute dependency.name
        2. list all flux jobs (I know, I know) and get the job id
        3. Update the jobspec to have it.
        4. Raise error if the name does not exist.

        Bullet 2 is a bad design and thus this is only for experimentation.
        """
        dependency_name = jobspec.attributes["system"].get("dependency", {}).get("name")
        if not dependency_name:
            return

        # We are going to add the first matched job as a dependency
        import flux
        import flux.job

        handle = flux.Flux()
        for job in flux.job.list.job_list(handle).get()["jobs"]:
            if job["name"] == dependency_name:
                jobspec.attributes["system"]["dependencies"] = [
                    {"scheme": "afterok", "value": str(job["id"])}
                ]
                return

        raise ValueError(f"Job with name {dependency_name} is not known")


class Frobnicator(FrobnicatorPlugin):
    def __init__(self, parser):
        super().__init__(parser)

    def configure(self, args, config):
        self.config = DependencyAdder(config)

    def frob(self, jobspec, user, urgency, flags):
        """This frobber is looking for an attribute "dependency.name"""
        self.config.add_dependency(jobspec)
