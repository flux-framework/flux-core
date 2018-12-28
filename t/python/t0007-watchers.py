#!/usr/bin/env python
import unittest

import flux
from subflux import rerun_under_flux


def __flux_size():
    return 2


class TestTimer(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        self.f = flux.Flux()

    @classmethod
    def tearDownClass(self):
        self.f.close()

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


if __name__ == "__main__":
    if rerun_under_flux(__flux_size()):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner())
