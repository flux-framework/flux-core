#!/usr/bin/env python3
###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""Unit tests for Scheduler pool-class selection.

Tests _pool_class_from_uri, _pool_class_from_writer, and _make_pool
without a running Flux instance.  All three methods are pure Python and
only use self for attribute access (pool_class, pool_kwargs, log) and
recursive calls to each other — a lightweight mock suffices.
"""

import json
import os
import shutil
import sys
import tempfile
import textwrap
import unittest

import subflux  # noqa: F401 - for PYTHONPATH
from flux.resource.ResourcePool import ResourcePool
from flux.resource.ResourcePoolImplementation import ResourcePoolImplementation
from flux.resource.Rv1Pool import Rv1Pool
from flux.scheduler import Scheduler
from pycotap import TAPTestRunner


class _MockSched:
    """Minimal stand-in for Scheduler that exercises pool-selection methods."""

    pool_class = None
    pool_kwargs = {}

    @staticmethod
    def log(level, msg):
        pass

    _pool_class_from_uri = Scheduler._pool_class_from_uri
    _pool_class_from_writer = Scheduler._pool_class_from_writer
    _make_pool = Scheduler._make_pool


# Minimal single-node R used as input to _make_pool.
R_SIMPLE = {
    "version": 1,
    "execution": {
        "R_lite": [{"rank": "0", "children": {"core": "0-3"}}],
        "starttime": 0,
        "expiration": 0,
        "nodelist": ["node0"],
    },
}


class TestPoolClassFromUri(unittest.TestCase):
    def setUp(self):
        self.sched = _MockSched()
        self.tmpdir = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.tmpdir)

    def _write_pool_file(self, filename, content):
        path = os.path.join(self.tmpdir, filename)
        with open(path, "w") as f:
            f.write(textwrap.dedent(content))
        return path

    def _pool_file_content(self, classname):
        return f"""\
            from flux.resource.Rv1Pool import Rv1Pool
            class {classname}(Rv1Pool):
                pass
            pool_class = {classname}
            """

    def test_missing_module_returns_none(self):
        """Unresolvable module URI returns None without raising."""
        self.assertIsNone(self.sched._pool_class_from_uri("no_such_module_xyz"))

    def test_module_without_pool_class_attr_returns_none(self):
        """Module that exists but lacks pool_class returns None."""
        self.assertIsNone(self.sched._pool_class_from_uri("json"))

    def test_module_uri_pool_class_attr(self):
        """Plain module URI loads pool_class attribute from the module."""
        self._write_pool_file("mypool.py", self._pool_file_content("MyPool"))
        sys.path.insert(0, self.tmpdir)
        try:
            cls = self.sched._pool_class_from_uri("mypool")
            self.assertEqual(cls.__name__, "MyPool")
        finally:
            sys.path.pop(0)
            sys.modules.pop("mypool", None)

    def test_module_uri_explicit_class_name(self):
        """module:ClassName URI loads the named class from the module."""
        self._write_pool_file("mypool2.py", self._pool_file_content("MyPool2"))
        sys.path.insert(0, self.tmpdir)
        try:
            cls = self.sched._pool_class_from_uri("mypool2:MyPool2")
            self.assertEqual(cls.__name__, "MyPool2")
        finally:
            sys.path.pop(0)
            sys.modules.pop("mypool2", None)


class TestPoolClassFromWriter(unittest.TestCase):
    def setUp(self):
        self.sched = _MockSched()
        self.tmpdir = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.tmpdir)

    def _write_pool_file(self, filename, classname):
        path = os.path.join(self.tmpdir, filename)
        with open(path, "w") as f:
            f.write(
                f"from flux.resource.Rv1Pool import Rv1Pool\n"
                f"class {classname}(Rv1Pool):\n"
                f"    pass\n"
                f"pool_class = {classname}\n"
            )
        return path

    def test_no_scheduling_key_returns_none(self):
        """R without a scheduling key returns None."""
        self.assertIsNone(self.sched._pool_class_from_writer(R_SIMPLE))

    def test_scheduling_without_writer_tries_fluxion(self):
        """scheduling key without writer defaults to fluxion; returns None if not installed."""
        R = dict(R_SIMPLE, scheduling={})
        # fluxion is not installed in the test environment
        self.assertIsNone(self.sched._pool_class_from_writer(R))

    def test_scheduling_with_module_writer(self):
        """scheduling.writer pointing to a module URI loads the class."""
        self._write_pool_file("writerpool.py", "WriterPool")
        sys.path.insert(0, self.tmpdir)
        try:
            R = dict(R_SIMPLE, scheduling={"writer": "writerpool"})
            cls = self.sched._pool_class_from_writer(R)
            self.assertEqual(cls.__name__, "WriterPool")
        finally:
            sys.path.pop(0)
            sys.modules.pop("writerpool", None)

    def test_json_string_input(self):
        """R supplied as a JSON string is parsed before inspection."""
        R = dict(R_SIMPLE, scheduling={})
        self.assertIsNone(self.sched._pool_class_from_writer(json.dumps(R)))

    def test_non_dict_non_str_returns_none(self):
        """Non-dict, non-str input returns None without raising."""
        self.assertIsNone(self.sched._pool_class_from_writer(42))
        self.assertIsNone(self.sched._pool_class_from_writer(None))


class TestMakePool(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.tmpdir)

    def _make_sched(self, pool_class=None, pool_kwargs=None):
        sched = _MockSched()
        sched.pool_class = pool_class
        sched.pool_kwargs = dict(pool_kwargs or {})
        return sched

    def _write_pool_file(self, filename, classname):
        path = os.path.join(self.tmpdir, filename)
        with open(path, "w") as f:
            f.write(
                f"from flux.resource.Rv1Pool import Rv1Pool\n"
                f"class {classname}(Rv1Pool):\n"
                f"    pass\n"
                f"pool_class = {classname}\n"
            )
        return path

    def test_explicit_pool_class_wins(self):
        """pool_class attribute takes priority over scheduling.writer."""

        class WinPool(ResourcePool):
            pass

        R = dict(
            R_SIMPLE,
            scheduling={"writer": "nonexistent_sched_module"},
        )
        sched = self._make_sched(pool_class=WinPool)
        pool = sched._make_pool(R)
        self.assertIsInstance(pool, WinPool)

    def test_writer_uri_used_when_no_pool_class(self):
        """scheduling.writer module URI is consulted when pool_class is None."""
        self._write_pool_file("writermod2.py", "WriterMod2")
        sys.path.insert(0, self.tmpdir)
        try:
            R = dict(R_SIMPLE, scheduling={"writer": "writermod2"})
            sched = self._make_sched()
            pool = sched._make_pool(R)
            self.assertEqual(type(pool).__name__, "WriterMod2")
        finally:
            sys.path.pop(0)
            sys.modules.pop("writermod2", None)

    def test_default_pool_when_no_class_no_writer(self):
        """Falls back to the default ResourcePool version dispatch."""
        sched = self._make_sched()
        pool = sched._make_pool(R_SIMPLE)
        self.assertIsInstance(pool, ResourcePool)

    def test_pool_kwargs_forwarded_to_explicit_pool_class(self):
        """pool_kwargs are forwarded to pool_class constructor."""
        received = {}

        class TrackingPool(ResourcePool):
            def __init__(self, R, log=None, **kwargs):
                received.update(kwargs)
                super().__init__(R, log=log)

        sched = self._make_sched(pool_class=TrackingPool, pool_kwargs={"foo": "bar"})
        sched._make_pool(R_SIMPLE)
        self.assertEqual(received, {"foo": "bar"})

    def test_pool_implementation_subclass_accepted(self):
        """ResourcePoolImplementation subclass is accepted as pool_class."""

        class ImplPool(Rv1Pool):
            def __init__(self, *args, **kwargs):
                super().__init__(*args, **kwargs)
                self.impl = self  # required: scheduler accesses pool.impl directly

        # Rv1Pool is a ResourcePoolImplementation subclass but NOT a ResourcePool
        # subclass — this was rejected before the fix.
        self.assertFalse(issubclass(ImplPool, ResourcePool))
        self.assertTrue(issubclass(ImplPool, ResourcePoolImplementation))
        sched = self._make_sched(pool_class=ImplPool)
        pool = sched._make_pool(R_SIMPLE)
        self.assertIsInstance(pool, ImplPool)

    def test_pool_implementation_missing_impl_raises_value_error(self):
        """ResourcePoolImplementation subclass that omits self.impl = self raises ValueError."""

        class BrokenImplPool(Rv1Pool):
            def __init__(self, *args, **kwargs):
                super().__init__(*args, **kwargs)
                # deliberately omits self.impl = self

        sched = self._make_sched(pool_class=BrokenImplPool)
        with self.assertRaises(ValueError):
            sched._make_pool(R_SIMPLE)

    def test_invalid_pool_class_raises_value_error(self):
        """Non-ResourcePool, non-ResourcePoolImplementation pool_class raises ValueError."""
        sched = self._make_sched(pool_class=str)
        with self.assertRaises(ValueError):
            sched._make_pool(R_SIMPLE)


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())
