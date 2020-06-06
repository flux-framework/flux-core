#!/usr/bin/env python

###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import unittest
import os
import signal
import flux
from subflux import rerun_under_flux


def __flux_size():
    return 2


class TestTimer(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        self.f = flux.Flux()

    def test_timer_add_negative(self):
        """Add a negative timer"""
        with self.assertRaises(EnvironmentError):
            self.f.timer_watcher_create(
                -500, lambda x, y: x.fatal_error("timer should not run")
            )

    def test_s1_0_timer_add(self):
        """Add a timer"""
        with self.f.timer_watcher_create(
            10000, lambda x, y, z, w: x.fatal_error("timer should not run")
        ) as tid:
            self.assertIsNotNone(tid)

    def test_s1_1_timer_remove(self):
        """Remove a timer"""
        to = self.f.timer_watcher_create(
            10000, lambda x, y: x.fatal_error("timer should not run")
        )
        to.stop()
        to.destroy()

    def test_timer_with_reactor(self):
        """Register a timer and run the reactor to ensure it can stop it"""
        timer_ran = [False]

        def cb(x, y, z, w):
            timer_ran[0] = True
            x.reactor_stop(self.f.get_reactor())

        with self.f.timer_watcher_create(0.1, cb) as timer:
            self.assertIsNotNone(timer, msg="timeout create")
            ret = self.f.reactor_run(self.f.get_reactor(), 0)
            self.assertEqual(ret, 0, msg="Reactor exit")
            self.assertTrue(timer_ran[0], msg="Timer did not run successfully")

    def test_msg_watcher_unicode(self):
        with self.f.msg_watcher_create(
            lambda handle, x, y, z: handle.fatal_error("cb should not run"),
            topic_glob=u"foo.*",
        ) as mw:
            self.assertIsNotNone(mw)

    def test_msg_watcher_bytes(self):
        with self.f.msg_watcher_create(
            lambda handle, x, y, z: handle.fatal_error("cb should not run"),
            topic_glob=b"foo.*",
        ) as mw:
            self.assertIsNotNone(mw)


class TestSignal(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        self.f = flux.Flux()

    def test_signal_watcher_invalid(self):
        """Add an invalid signal"""
        with self.assertRaises(EnvironmentError):
            self.f.signal_watcher_create(
                -500, lambda x, y: x.fatal_error("signal should not fire")
            )
        with self.assertRaises(EnvironmentError):
            self.f.signal_watcher_create(
                0, lambda x, y: x.fatal_error("signal should not fire")
            )
        with self.assertRaises(EnvironmentError):
            self.f.signal_watcher_create(
                500, lambda x, y: x.fatal_error("signal should not fire")
            )

    def test_s0_signal_watcher_add(self):
        """Add a signal watcher"""
        with self.f.signal_watcher_create(
            2, lambda x, y, z, w: x.fatal_error("signal should not fire")
        ) as sigw:
            self.assertIsNotNone(sigw)

    def test_s1_signal_watcher_remove(self):
        """Remove a timer"""
        sigw = self.f.signal_watcher_create(
            2, lambda x, y: x.fatal_error("signal should not fire")
        )
        sigw.stop()
        sigw.destroy()

    def test_signal_watcher(self):
        cb_called = [False]

        def cb(handle, watcher, signum, args):
            cb_called[0] = True
            handle.reactor_stop()

        def raise_signal(handle, wathcer, revents, args):
            os.kill(os.getpid(), signal.SIGUSR1)

        def stop(h, w, r, y):
            h.reactor_stop()

        with self.f.signal_watcher_create(signal.SIGUSR1, cb) as watcher:
            self.assertIsNotNone(watcher, msg="signalWatcher create")
            to1 = self.f.timer_watcher_create(0.10, raise_signal)
            to2 = self.f.timer_watcher_create(0.15, stop)
            to1.start()
            to2.start()
            rc = self.f.reactor_run()
            self.assertTrue(rc >= 0, msg="reactor exit")
            self.assertTrue(cb_called[0], "Signal Watcher Called")


if __name__ == "__main__":
    if rerun_under_flux(__flux_size()):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner())


# vi: ts=4 sw=4 expandtab
