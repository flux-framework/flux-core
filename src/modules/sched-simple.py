###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""Default Flux scheduler broker module (Python reimplementation).

A thin wrapper around :class:`FIFOScheduler` (from sched-fifo) that adds
debug hooks matching the legacy C sched-simple module's test interface.

Load with::

    flux module load sched-simple [queue-depth=N|unlimited] [log-level=LEVEL]

For backwards compatibility with the legacy C sched-simple module, the
following queue-depth aliases are also accepted::

    flux module load sched-simple mode=unlimited
    flux module load sched-simple mode=limited=N
"""

import errno
import importlib.util
import os
from flux.job import JobID

# sched-fifo is co-installed in the same directory; the hyphen in its name
# prevents a plain import statement, so load it explicitly by path.
_spec = importlib.util.spec_from_file_location(
    "sched_fifo",
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "sched-fifo.py"),
)
_mod = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_mod)
FIFOScheduler = _mod.FIFOScheduler

# Debug bits (set/clear via: flux module debug --setbit N sched-simple)
DEBUG_FAIL_ALLOC = 1  # while set, all alloc requests are denied
DEBUG_ANNOTATE_REASON_PENDING = 2  # while set, annotate pending jobs
DEBUG_EXPIRATION_UPDATE_DENY = 4  # while set, sched.expiration RPCs are denied


class SimpleScheduler(FIFOScheduler):
    """Priority-FIFO scheduler with C sched-simple compatible debug hooks.

    Extends :class:`FIFOScheduler` with the debug interface used by the
    flux-core test suite to exercise scheduler edge cases.

    Overrides:
        hello: emit a C sched-simple compatible DEBUG log line after registering
            the alloc.
        forecast: annotate pending jobs with reason/position when
            DEBUG_ANNOTATE_REASON_PENDING is set.
        expiration: honour DEBUG_EXPIRATION_UPDATE_DENY for testing.
        _try_alloc: honour DEBUG_FAIL_ALLOC for testing.
    """

    def __init__(self, h, *args):
        super().__init__(h, *args)
        for arg in list(self._pending_args):
            if arg == "test-hello-nopartial":
                self.hello_partial_ok = False
                self._pending_args.remove(arg)

    def hello(self, jobid, priority, userid, t_submit, R):
        """Register alloc and emit C sched-simple compatible DEBUG hello log."""
        super().hello(jobid, priority, userid, t_submit, R)
        self.log.debug(
            f"hello: id={JobID(jobid).f58} priority={priority} "
            f"userid={userid} t_submit={t_submit:.1f}",
        )

    def forecast(self):
        if self.debug_test(DEBUG_ANNOTATE_REASON_PENDING):
            # _queue is a heapq; sort to visit jobs in priority order so that
            # jobs_ahead reflects true queue position (0 = head).
            for jobs_ahead, job in enumerate(sorted(self._queue)):
                job.request.annotate(
                    {
                        "sched": {
                            "reason_pending": "insufficient resources",
                            "jobs_ahead": jobs_ahead,
                        }
                    }
                )
                yield
            return
        yield from super().forecast()

    def expiration(self, msg, jobid, expiration):
        if self.debug_test(DEBUG_EXPIRATION_UPDATE_DENY):
            self.handle.respond_error(
                msg, errno.EINVAL, "Rejecting expiration update for testing"
            )
            return
        super().expiration(msg, jobid, expiration)

    def _try_alloc(self, job):
        if self.debug_test(DEBUG_FAIL_ALLOC):
            job.request.deny("DEBUG_FAIL_ALLOC")
            return True
        return super()._try_alloc(job)


def mod_main(h, *args):
    SimpleScheduler(h, *args).run()
