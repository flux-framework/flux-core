###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""Support for flux-run-{prolog,epilog,housekeeping}.

These drivers run as root under flux-imp run. Each starts the
flux-{prolog,epilog,housekeeping}@ systemd unit for a job and passes the
environment provided by the IMP to the unit through an EnvironmentFile=.

In the EnvironmentFile=, each value is double-quoted with only '\\' and '"'
escaped. Inside double quotes, systemd's parser takes every other byte,
including a raw newline, literally, so a value can never be mistaken for
the start of a new NAME=value assignment. Quoting cannot protect names, so
entries whose name is not a valid shell variable name are dropped.
"""

import os
import re
import signal
import subprocess
import sys
import tempfile

from flux.job import JobID

_VALID_NAME = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
_SIGNALS = (signal.SIGINT, signal.SIGTERM)
# Formats of job variables set by the job manager: a decimal uid and an
# idset of broker ranks encoded with ranges (e.g. 0-3,5). Their values come
# from the instance and are checked before they reach scripts run as root.
_JOB_VALUE_FORMATS = {
    "FLUX_JOB_USERID": re.compile(r"[0-9]+"),
    "FLUX_JOB_RANKS": re.compile(r"[0-9]+(-[0-9]+)?(,[0-9]+(-[0-9]+)?)*"),
}


class Terminated(BaseException):
    """Raised from a signal handler when the driver is asked to terminate.

    Like KeyboardInterrupt, this derives from BaseException rather than
    Exception so that handlers for ordinary errors, e.g. an OSError from
    write_env_file(), do not catch it.
    """


def _raise_terminated(signum, frame):
    raise Terminated


def _ignore(signum, frame):
    pass


def read_environ():
    """Return the environment this process was exec'd with, as a dict.

    This is read from /proc/self/environ rather than taken from os.environ
    because the Python interpreter may modify its own environment before
    any of this code runs: with no locale variables set, as is typical
    under flux-imp run, PEP 538 locale coercion adds LC_CTYPE=C.UTF-8.
    That cannot be disabled under -I, and cannot be undone afterward
    since LC_CTYPE may also have been passed legitimately. The unit must
    receive exactly the environment the IMP provided, and
    /proc/self/environ is the unmodified block passed to execve(2).

    Values are decoded with os.fsdecode(), as os.environ does, so arbitrary
    bytes round-trip exactly through os.fsencode() in write_env_file().
    """
    with open("/proc/self/environ", "rb") as fh:
        entries = os.fsdecode(fh.read()).split("\0")
    return dict(e.split("=", 1) for e in entries if "=" in e)


def normalize_jobid(environ):
    """Convert FLUX_JOB_ID in environ to f58plain in place and return it.

    FLUX_JOB_ID may arrive in any valid encoding (e.g. decimal from
    housekeeping, f58 with a multibyte prefix from the perilog plugin),
    but the unit and its scripts always see the plain ASCII f58 form.
    Return None if FLUX_JOB_ID is unset or empty. Raise ValueError if
    it is not a valid jobid.
    """
    value = environ.get("FLUX_JOB_ID")
    if not value:
        return None
    try:
        jobid = JobID(value).f58plain
    except ValueError:
        raise ValueError(f"invalid FLUX_JOB_ID: {value!r}") from None
    environ["FLUX_JOB_ID"] = jobid
    return jobid


def check_job_values(environ):
    """Raise ValueError if FLUX_JOB_USERID or FLUX_JOB_RANKS in environ is
    set to a value not in the format produced by the job manager."""
    for name, pattern in _JOB_VALUE_FORMATS.items():
        value = environ.get(name)
        if value and not pattern.fullmatch(value):
            raise ValueError(f"invalid {name}: {value!r}")


def format_env_line(name, value):
    """Return a NAME="value" line for systemd's EnvironmentFile= parser."""
    value = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'{name}="{value}"\n'


def write_env_file(path, environ):
    """Write environ to path as a root-only EnvironmentFile=.

    The file is written to a temporary file in the same directory, created
    exclusively with mode 0600, and then renamed over path. The rename
    replaces any existing file or symlink at path without following it, and
    a failure leaves any existing file untouched.

    Values from os.environ represent bytes not valid in the filesystem
    encoding as surrogate escapes, so lines are encoded with os.fsencode()
    to reproduce them exactly.
    """
    dirname, basename = os.path.split(path)
    fd, tmp = tempfile.mkstemp(dir=dirname, prefix=f".{basename}.")
    try:
        with os.fdopen(fd, "wb") as fh:
            for name, value in environ.items():
                fh.write(os.fsencode(format_env_line(name, value)))
        os.replace(tmp, path)
    except BaseException:
        os.unlink(tmp)
        raise


class PerilogProc:
    """Drives one flux-{prolog,epilog,housekeeping}@<jobid> systemd unit."""

    def __init__(self, kind, runstatedir, systemctl="systemctl", environ=None):
        self.environ = read_environ() if environ is None else environ
        jobid = normalize_jobid(self.environ)
        check_job_values(self.environ)
        # Without a jobid, include the pid so that concurrent invocations
        # do not share a unit or an environment file. The separator must
        # survive unescaping of the instance name, since the unit refers to
        # its environment file with %I, which turns '-' into '/'.
        self.unitname = f"flux-{kind}@{jobid or f'unknown_{os.getpid()}'}"
        self.envfile = os.path.join(runstatedir, f"{self.unitname}.env")
        self.systemctl = systemctl

    def run(self):
        """Start the unit and wait for it to complete.

        On SIGINT or SIGTERM, stop the unit and return 1. Otherwise return
        the exit status of systemctl start.
        """
        for sig in _SIGNALS:
            signal.signal(sig, _raise_terminated)
        try:
            write_env_file(self.envfile, self.environ)
            proc = subprocess.Popen([self.systemctl, "start", self.unitname, "--quiet"])
            rc = proc.wait()
        except Terminated:
            # Let an in-progress stop complete if signaled again. A Python
            # handler rather than SIG_IGN is used so that systemctl stop
            # still receives signals with the default disposition.
            for sig in _SIGNALS:
                signal.signal(sig, _ignore)
            subprocess.run([self.systemctl, "stop", self.unitname])
            return 1
        # A child killed by signal N is reported as 128+N.
        return 128 - rc if rc < 0 else rc


def main(kind, runstatedir):
    """Entry point for flux-run-{prolog,epilog,housekeeping}."""
    prog = f"flux-run-{kind}"
    try:
        proc = PerilogProc(kind, runstatedir)
    except ValueError as exc:
        print(f"{prog}: {exc}", file=sys.stderr)
        return 1
    try:
        return proc.run()
    except OSError as exc:
        print(f"{prog}: {exc}", file=sys.stderr)
        return 1


# vi: ts=4 sw=4 expandtab
