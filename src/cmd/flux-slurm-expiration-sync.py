###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""
slurm-expiration-sync

Poll squeue for remaining time of the enclosing Slurm job and notify
the resource module via RPC when the expiration changes, so that
flux_job_timeleft(3) reflects the Slurm job's expiration.

"""

import argparse
import logging
import signal
import sys
import time

import flux
import flux.slurm as slurm
from flux.util import fsd

POLL_INTERVAL_DEFAULT = 60  # seconds
CHANGE_THRESHOLD = 5.0  # seconds -- ignore jitter smaller than this

LOGGER = logging.getLogger("slurm-expiration-sync")


def send_expiration_update(fh, expiration):
    """Send updated expiration timestamp to the resource module."""
    try:
        f = fh.rpc(
            "resource.expiration-update",
            {"expiration": expiration},
            0,
            0,
        )
        f.get()
        LOGGER.debug("expiration updated to %.3f", expiration)
    except Exception as e:
        LOGGER.warning("resource.expiration-update failed: %s", e)


def make_poll_cb(jobid, fh):
    last_expiration = [None]

    def poll_and_update(fh, watcher, revents, _args):
        timeleft = slurm.slurm_timeleft(jobid)
        if timeleft is None:
            return

        expiration = time.time() + timeleft
        last = last_expiration[0]

        if last is None or abs(expiration - last) > CHANGE_THRESHOLD:
            LOGGER.debug(
                "Slurm timeleft changed: %s remaining, expiration=%.3f",
                fsd(timeleft),
                expiration,
            )
            send_expiration_update(fh, expiration)
            last_expiration[0] = expiration

    return poll_and_update


@flux.util.CLIMain(LOGGER)
def main():

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--poll-interval",
        type=float,
        default=POLL_INTERVAL_DEFAULT,
        metavar="SECONDS",
        help="squeue polling interval in seconds (default: %(default)s)",
    )
    parser.add_argument(
        "--jobid", "-j", metavar="JOBID", help="Operate on Slurm job JOBID"
    )
    parser.add_argument(
        "--verbose", "-v", action="store_true", help="Enable debug logging"
    )
    args = parser.parse_args()

    LOGGER.setLevel(args.verbose and logging.DEBUG or logging.INFO)

    try:
        jobid = int(args.jobid)
    except (ValueError, TypeError):
        LOGGER.error("Invalid or missing --jobid")
        sys.exit(1)

    fh = flux.Flux()

    # Probe once at startup to confirm squeue is reachable and we have a
    # valid time limit before entering the poll loop.
    timeleft = slurm.slurm_timeleft(jobid)
    if timeleft is None:
        LOGGER.info("No Slurm time limit detected (unlimited or error), exiting")
        # Update expiration to 0 (unlimited) so that the resource module will
        # post a resource-update event allowing rc1 task to exit.
        send_expiration_update(fh, 0.0)
        sys.exit(0)

    LOGGER.debug("Slurm job %d detected, %s remaining at startup", jobid, fsd(timeleft))

    poll_cb = make_poll_cb(jobid, fh)

    # Fire immediately to push the initial expiration, then on each interval
    timer = fh.timer_watcher_create(
        0,
        poll_cb,
        repeat=args.poll_interval,
    )
    timer.start()

    sigwatcher = fh.signal_watcher_create(
        signal.SIGTERM, lambda handle, x, y, watcher: handle.reactor_stop()
    )
    sigwatcher.start()

    try:
        fh.reactor_run()
    except KeyboardInterrupt:
        # exit normally on ctrl-c
        pass


if __name__ == "__main__":
    main()
