##############################################################
# Copyright 2023 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import argparse
import json
import logging
import math
import sys

import flux
import flux.job
import flux.util

LOGGER = logging.getLogger("flux-update")


class JobspecUpdates:
    """
    Convenience class for building a jobspec-update payload from a
    set of KEY=VALUE pairs on the command line, and a method to send
    the update as a request to the job manager.
    """

    #  Mapping of short key names, i.e. as given on the command line,
    #  to full dotted-path location in jobspec.
    #
    #  Note: If a key doesn't exist in this mapping, but also does not start
    #  with 'attributes.', 'resources.' or 'tasks.', then 'attributes.system'
    #  is assumed.
    #
    key_aliases = {"name": "attributes.system.job.name"}

    def __init__(self, jobid, flux_handle=None):
        self._flux_handle = flux_handle
        self.jobid = jobid
        self.updates = None
        self.jobspec = None

    @property
    def flux_handle(self):
        if self._flux_handle is None:
            self._flux_handle = flux.Flux()
        return self._flux_handle

    def _fetch_jobspec(self, key):
        """
        Fetch dotted key 'key' in jobspec for this job.  Note that
        job_kvs_lookup() will apply `jobspec-update` events from the
        eventlog in the returned jobspec.
        """
        if self.jobspec is None:
            lookup = flux.job.job_kvs_lookup(
                self.flux_handle, jobid=self.jobid, keys=["jobspec"]
            )
            self.jobspec = flux.job.JobspecV1(**lookup["jobspec"])

        return self.jobspec.getattr(key)

    def update_attributes_system_duration(self, value):
        """
        Handle a duration update.

        If update begins with "+" or "-", then get duration from jobspec and
        increase or decrease by the amount of the remaining argument. O/w,
        treat value as an explicit new duration.
        """
        result = None
        if value.startswith(("-", "+")):
            # relative update, fetch value first
            duration = self._fetch_jobspec("attributes.system.duration")
            if duration == 0:
                raise ValueError(
                    f"duration for {self.jobid} is unlimited, "
                    f"can't update by {value}"
                )

            arg = flux.util.parse_fsd(value[1:])
            if value.startswith("-"):
                result = duration - arg
                if result <= 0.0:
                    duration = flux.util.fsd(duration)
                    raise ValueError(
                        f"current duration for {self.jobid} ({duration})"
                        f" cannot be reduced by {value[1:]}"
                    )
            else:
                result = duration + arg
        else:
            result = flux.util.parse_fsd(value)

        # An unlimited duration is represented as 0. in jobspec, so
        # check for infinity here and replace with 0.
        #
        if math.isinf(result):
            result = 0.0
        return result

    def add_update(self, key, value):
        """
        Append an update to the current updates object.
        """
        if self.updates is None:
            self.updates = {}

        #  Handle any special keys aliases
        if key in self.key_aliases:
            key = self.key_aliases[key]

        #  If key doesn't start with attributes, resources, or tasks,
        #  assume 'attributes.system.' for convenience:
        if not key.startswith(("attributes.", "resources.", "tasks.")):
            key = f"attributes.system.{key}"
        try:
            #  Use any function update_attributes_system_blah() if
            #  registered to process the value:
            #
            function_signature = "update_" + key.replace(".", "_")
            value = getattr(self, function_signature)(value)
        except AttributeError:
            #  Otherwise, attempt to load value as JSON:
            #
            try:
                value = json.loads(value)
            except json.decoder.JSONDecodeError:
                #  Otherwise, load value as string:
                #
                value = str(value)
        self.updates[key] = value

    def items(self):
        """
        Convenience wrapper to return a copy of the current update
        dictionary key, value pairs
        """
        return self.updates.items()

    def to_json(self):
        return json.dumps(self.updates)

    def send_rpc(self):
        payload = {"id": self.jobid, "updates": self.updates}
        return self.flux_handle.rpc("job-manager.update", payload)


def parse_args():
    parser = argparse.ArgumentParser(
        prog="flux-update", formatter_class=flux.util.help_formatter()
    )
    parser.add_argument(
        "-n",
        "--dry-run",
        action="store_true",
        help="Do not apply any updates, just emit update payload to stdout",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        default=0,
        help="Be more verbose. Log updated items after success.",
    )
    parser.add_argument(
        "jobid",
        metavar="JOBID",
        type=flux.job.JobID,
        help="Target jobid",
    )
    parser.add_argument(
        "updates",
        metavar="KEY=VALUE",
        type=str,
        nargs="+",
        help="Requested jobspec updates in KEY=VALUE form",
    )
    return parser.parse_args()


@flux.util.CLIMain(LOGGER)
def main():
    sys.stdout = open(
        sys.stdout.fileno(), "w", encoding="utf8", errors="surrogateescape"
    )
    sys.stderr = open(
        sys.stderr.fileno(), "w", encoding="utf8", errors="surrogateescape"
    )

    args = parse_args()

    updates = JobspecUpdates(args.jobid)

    for arg in args.updates:
        key, _, value = arg.partition("=")
        updates.add_update(key, value)

    if args.dry_run:
        print(updates.to_json())
        sys.exit(0)

    updates.send_rpc().get()
    if args.verbose:
        for key, value in updates.items():
            LOGGER.info(f"updated {key} to {value}")


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
