#!/usr/bin/env python3
###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import datetime
import json
import os
import pathlib
import unittest

import subflux  # noqa: F401 - sets up PYTHONPATH
from flux.job import JobspecV1
from flux.job.Jobspec import Jobspec
from pycotap import TAPTestRunner

# Minimal valid v1 jobspec for use as a test fixture
BASIC_JOBSPEC = json.dumps(
    {
        "version": 1,
        "resources": [
            {
                "type": "slot",
                "count": 1,
                "label": "task",
                "with": [{"type": "core", "count": 1}],
            }
        ],
        "tasks": [{"command": ["app"], "slot": "foo", "count": {"per_slot": 1}}],
        "attributes": {"system": {"duration": 0}},
    }
)


class TestJobspec(unittest.TestCase):
    """Unit tests for the Jobspec API that do not require a running Flux instance.

    These tests were formerly part of t0010-job.py which requires a Flux instance
    for most of its tests.
    """

    def _basic(self):
        return Jobspec.from_yaml_stream(BASIC_JOBSPEC)

    def test_01_valid_duration(self):
        """Test setting Jobspec duration to various valid values"""
        jobspec = self._basic()
        for duration in (100, 100.5):
            delta = datetime.timedelta(seconds=duration)
            for x in [duration, delta, "{}s".format(duration)]:
                jobspec.duration = x
                # duration setter converts value to a float
                self.assertEqual(jobspec.duration, float(duration))

    def test_02_invalid_duration(self):
        """Test setting Jobspec duration to various invalid values and types"""
        jobspec = self._basic()
        for duration in (-100, -100.5, datetime.timedelta(seconds=-5), "10h5m"):
            with self.assertRaises(ValueError):
                jobspec.duration = duration
        for duration in ([], {}):
            with self.assertRaises(TypeError):
                jobspec.duration = duration

    def test_03_cwd_pathlib(self):
        """Test setting cwd to a pathlib.PosixPath"""
        jobspec = self._basic()
        cwd = pathlib.PosixPath("/tmp")
        jobspec.cwd = cwd
        self.assertEqual(jobspec.cwd, os.fspath(cwd))

    def test_04_environment(self):
        jobspec = self._basic()
        new_env = {"HOME": "foo", "foo": "bar"}
        jobspec.environment = new_env
        self.assertEqual(jobspec.environment, new_env)

    def test_05_queue(self):
        jobspec = self._basic()
        jobspec.queue = "default"
        self.assertEqual(jobspec.queue, "default")

    def test_06_queue_invalid(self):
        jobspec = self._basic()
        with self.assertRaises(TypeError):
            jobspec.queue = 12

    def test_07_stdio_new_methods(self):
        """Test official getter/setter methods for stdio properties
        Ensure for now that output sets the alias "stdout", error sets "stderr"
        and input sets "stdin".
        """
        jobspec = self._basic()
        streams = {"error": "stderr", "output": "stdout", "input": "stdin"}
        for name in ("error", "output", "input"):
            stream = streams[name]
            self.assertEqual(getattr(jobspec, name), None)
            for path in ("foo.txt", "bar.md", "foo.json"):
                setattr(jobspec, name, path)
                self.assertEqual(getattr(jobspec, name), path)
                self.assertEqual(getattr(jobspec, stream), path)
            with self.assertRaises(TypeError):
                setattr(jobspec, name, None)

    def test_08_stdio(self):
        """Test getter/setter methods for stdio properties"""
        jobspec = self._basic()
        for stream in ("stderr", "stdout", "stdin"):
            self.assertEqual(getattr(jobspec, stream), None)
            for path in ("foo.txt", "bar.md", "foo.json"):
                setattr(jobspec, stream, path)
                self.assertEqual(getattr(jobspec, stream), path)
            with self.assertRaises(TypeError):
                setattr(jobspec, stream, None)

        with self.assertRaises(TypeError):
            jobspec.unbuffered = 1

        jobspec.unbuffered = True
        self.assertTrue(jobspec.unbuffered)
        self.assertEqual(
            jobspec.getattr("shell.options.output.stderr.buffer.type"), "none"
        )
        self.assertEqual(
            jobspec.getattr("shell.options.output.stdout.buffer.type"), "none"
        )
        self.assertEqual(jobspec.getattr("shell.options.output.batch-timeout"), 0.05)

        jobspec.unbuffered = False
        self.assertFalse(jobspec.unbuffered)

        # jobspec.unbuffered = True keeps modified batch-timeout
        jobspec.unbuffered = True
        jobspec.setattr_shell_option("output.batch-timeout", 1.0)
        jobspec.unbuffered = False
        self.assertFalse(jobspec.unbuffered)
        self.assertEqual(jobspec.getattr("shell.options.output.batch-timeout"), 1.0)

    def test_09_setattr_defaults(self):
        """Test setattr setting defaults"""
        jobspec = self._basic()
        jobspec.setattr("cow", 1)
        jobspec.setattr("system.cat", 2)
        jobspec.setattr("user.dog", 3)
        jobspec.setattr("attributes.system.chicken", 4)
        jobspec.setattr("attributes.user.duck", 5)
        jobspec.setattr("attributes.goat", 6)
        self.assertEqual(jobspec.getattr("cow"), 1)
        self.assertEqual(jobspec.getattr("system.cow"), 1)
        self.assertEqual(jobspec.getattr("attributes.system.cow"), 1)

        self.assertEqual(jobspec.getattr("cat"), 2)
        self.assertEqual(jobspec.getattr("system.cat"), 2)
        self.assertEqual(jobspec.getattr("attributes.system.cat"), 2)

        self.assertEqual(jobspec.getattr("user.dog"), 3)
        self.assertEqual(jobspec.getattr("attributes.user.dog"), 3)

        self.assertEqual(jobspec.getattr("chicken"), 4)
        self.assertEqual(jobspec.getattr("system.chicken"), 4)
        self.assertEqual(jobspec.getattr("attributes.system.chicken"), 4)

        self.assertEqual(jobspec.getattr("user.duck"), 5)
        self.assertEqual(jobspec.getattr("attributes.user.duck"), 5)

        self.assertEqual(jobspec.getattr("attributes.goat"), 6)

    def test_10_str(self):
        """Test string representation of a basic jobspec"""
        jobspec = self._basic()
        jobspec.setattr("cow", 1)
        jobspec.setattr("system.cat", 2)
        jobspec.setattr("user.dog", 3)
        jobspec.setattr("attributes.system.chicken", 4)
        jobspec.setattr("attributes.user.duck", 5)
        jobspec.setattr("attributes.goat", 6)
        self.assertEqual(
            str(jobspec),
            "{'resources': [{'type': 'slot', 'count': 1, 'label': 'task', 'with': [{'type': 'core', 'count': 1}]}], 'tasks': [{'command': ['app'], 'slot': 'foo', 'count': {'per_slot': 1}}], 'attributes': {'system': {'duration': 0, 'cow': 1, 'cat': 2, 'chicken': 4}, 'user': {'dog': 3, 'duck': 5}, 'goat': 6}, 'version': 1}",
        )

    def test_11_repr(self):
        """Test __repr__ method of Jobspec"""
        jobspec = self._basic()
        self.assertEqual(eval(repr(jobspec)).jobspec, jobspec.jobspec)
        jobspec.cwd = "/foo/bar"
        jobspec.stdout = "/bar/baz"
        jobspec.duration = 1000.3133
        self.assertEqual(eval(repr(jobspec)).jobspec, jobspec.jobspec)

    def test_12_bad_extra_args(self):
        """Test extra Jobspec constructor args with bad values"""
        with self.assertRaises(ValueError):
            JobspecV1.from_command(["sleep", "0"], duration="1f")
        with self.assertRaises(ValueError):
            JobspecV1.from_command(["sleep", "0"], environment="foo")
        with self.assertRaises(ValueError):
            JobspecV1.from_command(["sleep", "0"], env_expand=1)
        with self.assertRaises(ValueError):
            JobspecV1.from_command(["sleep", "0"], rlimits=True)

    def test_13_environment_default(self):
        jobspec = JobspecV1.from_command(["sleep", "0"])
        self.assertEqual(jobspec.environment, dict(os.environ))

        jobspec = JobspecV1.from_command(["sleep", "0"], environment={})
        self.assertEqual(jobspec.environment, {})


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())
