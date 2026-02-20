###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import subprocess

SLURM_TIME_UNLIMITED = {"UNLIMITED", "NOT_SET", "INVALID"}


def _parse_slurm_time(timestr):
    """Parse Slurm time string (D-HH:MM:SS, HH:MM:SS, MM:SS) to seconds.
    Returns None if the time limit is unlimited or unparsable.
    """
    timestr = timestr.strip()
    if timestr in SLURM_TIME_UNLIMITED:
        return None
    days = 0
    if "-" in timestr:
        d, timestr = timestr.split("-", 1)
        days = int(d)
    parts = timestr.split(":")
    try:
        if len(parts) == 3:
            h, m, s = int(parts[0]), int(parts[1]), int(parts[2])
        elif len(parts) == 2:
            h, m, s = 0, int(parts[0]), int(parts[1])
        else:
            return None
    except ValueError:
        return None
    return days * 86400 + h * 3600 + m * 60 + s


def slurm_timeleft(jobid):
    """Return remaining time in seconds for jobid, or None if no time limit"""
    try:
        result = subprocess.run(
            ["squeue", "--noheader", f"--job={jobid}", "-o", "%L"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=10,
        )
        if result.returncode != 0:
            msg = result.stderr.decode("utf-8")
            raise RuntimeError(f"squeue failed: {msg}")
        return _parse_slurm_time(result.stdout.decode("utf-8"))
    except FileNotFoundError:
        raise RuntimeError("squeue not found in PATH")


# vi: ts=4 sw=4 expandtab
