#!/usr/bin/env python3
###############################################################
# Copyright 2024 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import unittest

import subflux  # noqa: F401
from flux.modprobe import DependencySolver, Task, TaskDB
from pycotap import TAPTestRunner


class TestTaskDB(unittest.TestCase):
    """Test TaskDB implementation"""

    def test_add_task_basic(self):
        """Add a task and retrieve it by name"""
        db = TaskDB()
        task = Task("test-task")
        db.add(task)
        retrieved = db.get("test-task")
        self.assertEqual(retrieved.name, "test-task")
        self.assertIs(retrieved, task)

    def test_add_task_with_provides(self):
        """Task provides multiple services"""
        db = TaskDB()
        task = Task("module-a", provides=["service-x", "service-y"])
        db.add(task)

        # Should be retrievable by name
        self.assertIs(db.get("module-a"), task)
        # Should be retrievable by any provides
        self.assertIs(db.get("service-x"), task)
        self.assertIs(db.get("service-y"), task)

    def test_get_nonexistent_raises(self):
        """Getting nonexistent service raises ValueError"""
        db = TaskDB()
        with self.assertRaises(ValueError) as ctx:
            db.get("nonexistent")
        self.assertIn("no such task or module", str(ctx.exception))

    def test_get_highest_priority(self):
        """Multiple alternatives - picks highest priority"""
        db = TaskDB()
        task_low = Task("module-a", provides=["service"], priority=10)
        task_high = Task("module-b", provides=["service"], priority=100)
        task_medium = Task("module-c", provides=["service"], priority=50)

        db.add(task_low)
        db.add(task_high)
        db.add(task_medium)

        # Should return highest priority
        retrieved = db.get("service")
        self.assertEqual(retrieved.name, "module-b")
        self.assertEqual(retrieved.priority, 100)

    def test_get_prefers_enabled(self):
        """Picks enabled task over higher priority disabled task"""
        db = TaskDB()
        task_high = Task("module-a", provides=["service"], priority=100)
        task_low = Task("module-b", provides=["service"], priority=10)

        db.add(task_high)
        db.add(task_low)

        # Disable the high priority task
        task_high.disabled = True

        # Should return lower priority enabled task
        retrieved = db.get("service")
        self.assertEqual(retrieved.name, "module-b")
        self.assertEqual(retrieved.priority, 10)

    def test_get_insertion_order_tiebreaker(self):
        """When priorities equal, later insertion wins"""
        db = TaskDB()
        task_first = Task("module-a", provides=["service"], priority=100)
        task_second = Task("module-b", provides=["service"], priority=100)

        db.add(task_first)
        db.add(task_second)

        # Should return later insertion when priorities equal
        retrieved = db.get("service")
        self.assertEqual(retrieved.name, "module-b")

    def test_update_preserves_order(self):
        """Update doesn't change insertion order"""
        db = TaskDB()
        task_first = Task("module-a", provides=["service"], priority=50)
        task_second = Task("module-b", provides=["service"], priority=100)

        db.add(task_first)
        db.add(task_second)

        # Update first task priority but not above second
        task_first.priority = 75
        db["module-a"] = task_first

        # Second should still win due to insertion order
        retrieved = db.get("service")
        self.assertEqual(retrieved.name, "module-b")

        # Update first task priority above second
        task_first.priority = 150
        db["module-a"] = task_first

        # Now first should win due to priority
        retrieved = db.get("service")
        self.assertEqual(retrieved.name, "module-a")

    def test_set_alternative(self):
        """Selecting alternative bumps priority"""
        db = TaskDB()
        task_a = Task("module-a", provides=["service"], priority=10)
        task_b = Task("module-b", provides=["service"], priority=20)
        task_c = Task("module-c", provides=["service"], priority=15)

        db.add(task_a)
        db.add(task_b)
        db.add(task_c)

        # Initially, module-b should win (highest priority)
        self.assertEqual(db.get("service").name, "module-b")

        # Select module-a as alternative
        db.set_alternative("service", "module-a")

        # Now module-a should win (priority bumped above max)
        self.assertEqual(db.get("service").name, "module-a")

    def test_set_alternative_propagates(self):
        """Setting alternative propagates to all provides"""
        db = TaskDB()
        task_a = Task("module-a", provides=["svc-x", "svc-y"], priority=10)
        task_b = Task("module-b", provides=["svc-x", "svc-y"], priority=20)

        db.add(task_a)
        db.add(task_b)

        # Select module-a for svc-x (should propagate to svc-y)
        db.set_alternative("svc-x", "module-a")

        # Both services should now return module-a
        self.assertEqual(db.get("svc-x").name, "module-a")
        self.assertEqual(db.get("svc-y").name, "module-a")

    def test_set_alternative_nonexistent_service(self):
        """Setting alternative for nonexistent service raises"""
        db = TaskDB()
        with self.assertRaises(ValueError) as ctx:
            db.set_alternative("nonexistent", "module-a")
        self.assertIn("no such service", str(ctx.exception))

    def test_set_alternative_nonexistent_module(self):
        """Setting alternative to nonexistent module raises"""
        db = TaskDB()
        task = Task("module-a", provides=["service"])
        db.add(task)

        with self.assertRaises(ValueError) as ctx:
            db.set_alternative("service", "nonexistent")
        self.assertIn("no module nonexistent provides", str(ctx.exception))

    def test_disable_task(self):
        """Disabling affects get()"""
        db = TaskDB()
        task_a = Task("module-a", provides=["service"])
        task_b = Task("module-b", provides=["service"])

        db.add(task_a)
        db.add(task_b)

        # Disable all tasks providing service
        db.disable("service")

        # Both should be disabled
        self.assertTrue(task_a.disabled)
        self.assertTrue(task_b.disabled)

        # get() should still return highest priority (even if disabled)
        result = db.get("service")
        self.assertIn(result.name, ["module-a", "module-b"])

    def test_enable_task(self):
        """Force enable overrides conditions"""
        db = TaskDB()
        task = Task("module-a", disabled=True)
        db.add(task)

        # Task is disabled
        self.assertTrue(task.disabled)

        # Force enable
        db.enable("module-a")

        # Should now be force-enabled
        self.assertTrue(task.force_enabled)

    def test_has_enabled_provider(self):
        """has_enabled_provider returns True if any non-disabled task provides service"""
        db = TaskDB()
        task_a = Task("module-a", provides=["service-x"])
        task_b = Task("module-b", provides=["service-y"])

        db.add(task_a)
        db.add(task_b)

        # Should find service-x
        self.assertTrue(db.has_enabled_provider(["module-a", "module-b"], "service-x"))
        # Should find service-y
        self.assertTrue(db.has_enabled_provider(["module-a", "module-b"], "service-y"))
        # Should not find nonexistent
        self.assertFalse(
            db.has_enabled_provider(["module-a", "module-b"], "nonexistent")
        )

    def test_has_enabled_provider_disabled_task(self):
        """has_enabled_provider returns False for disabled tasks"""
        db = TaskDB()
        task = Task("module-a", provides=["service"])
        task.disabled = True
        db.add(task)

        # Should not find service from disabled task
        self.assertFalse(db.has_enabled_provider(["module-a"], "service"))

    def test_has(self):
        """has() returns True if task/service exists"""
        db = TaskDB()
        task = Task("module-a", provides=["service-x"])
        db.add(task)

        # Should find by name
        self.assertTrue("module-a" in db)
        # Should find by provides
        self.assertTrue("service-x" in db)
        # Should not find nonexistent
        self.assertFalse("nonexistent" in db)

    def test_enable_does_not_cascade_to_requires(self):
        """Enable does not cascade to required tasks (by design)"""
        db = TaskDB()
        task_a = Task("module-a", requires=["module-b"], disabled=True)
        task_b = Task("module-b", disabled=True)

        db.add(task_a)
        db.add(task_b)

        # Enable module-a
        db.enable("module-a")

        # module-a should be force-enabled
        self.assertTrue(task_a.force_enabled)
        # module-b should NOT be force-enabled (by design)
        self.assertFalse(task_b.force_enabled)
        self.assertTrue(task_b.disabled)

    def test_disable_with_needs_semantics(self):
        """Disabling a task affects tasks that need it (handled by Modprobe)"""
        # Note: The cascading disable for 'needs' is handled in
        # Modprobe._process_needs(), not in TaskDB.
        # TaskDB.disable() only disables the specific task.
        db = TaskDB()
        task_a = Task("module-a")
        task_b = Task("module-b", needs=["module-a"])

        db.add(task_a)
        db.add(task_b)

        # Disable module-a at TaskDB level
        db.disable("module-a")

        # module-a is disabled
        self.assertTrue(task_a.disabled)
        # module-b is NOT automatically disabled by TaskDB
        # (that logic lives in Modprobe._process_needs)
        self.assertFalse(task_b.disabled)

    def test_set_alternative_priority_bump(self):
        """set_alternative() correctly bumps priority above alternatives"""
        from flux.modprobe import Module

        db = TaskDB()

        low = Module({"name": "low", "provides": ["service"]})
        high = Module({"name": "high", "priority": 500, "provides": ["service"]})

        db.add(low)
        db.add(high)

        # High priority module should win initially
        self.assertEqual(db.get("service").name, "high")

        # Set alternative to low priority module
        db.set_alternative("service", "low")

        # Now low should win despite lower original priority
        self.assertEqual(db.get("service").name, "low")

        # Verify priority was actually bumped above high
        task = db.get("low")
        self.assertGreater(task.priority, 500)

    def test_set_alternative_with_disabled(self):
        """set_alternative() works correctly when alternatives are disabled"""
        from flux.modprobe import Module

        db = TaskDB()

        sched_simple = Module(
            {"name": "sched-simple", "provides": ["sched", "feasibility"]}
        )

        advanced = Module(
            {
                "name": "advanced-scheduler",
                "priority": 500,
                "provides": ["sched", "feasibility"],
            }
        )

        db.add(sched_simple)
        db.add(advanced)

        # Disable the high-priority alternative
        db.disable("advanced-scheduler")

        # Low priority module should win when high priority is disabled
        self.assertEqual(db.get("feasibility").name, "sched-simple")

        # Set alternative to force sched-simple
        db.set_alternative("sched", "sched-simple")

        # Should still return sched-simple
        self.assertEqual(db.get("feasibility").name, "sched-simple")

        # Re-enable advanced-scheduler
        advanced.disabled = False

        # sched-simple should still win due to priority bump
        self.assertEqual(db.get("feasibility").name, "sched-simple")

    def test_add_preserves_priority_boost(self):
        """taskdb.add() preserves priority boost (boost modifies task.priority)"""
        from flux.modprobe import Module

        db = TaskDB()

        sched_simple = Module(
            {"name": "sched-simple", "provides": ["sched", "feasibility"]}
        )

        advanced = Module(
            {
                "name": "advanced-scheduler",
                "priority": 500,
                "provides": ["sched", "feasibility"],
            }
        )

        db.add(sched_simple)
        db.add(advanced)

        # Set alternative to force sched-simple (boosts task.priority)
        db.set_alternative("sched", "sched-simple")
        self.assertEqual(db.get("feasibility").name, "sched-simple")

        # Calling add() again still uses the boosted task.priority
        db.add(sched_simple)

        # Priority boost is preserved - sched-simple still wins
        self.assertEqual(db.get("feasibility").name, "sched-simple")

    def test_contains_operator(self):
        """__contains__ enables 'in' operator for existence checks"""
        db = TaskDB()
        task = Task("kvs", provides=["kvs-service"])
        db.add(task)

        # Test 'in' operator for task name
        self.assertTrue("kvs" in db)
        self.assertFalse("nonexistent" in db)

        # Test 'in' operator for provided services
        self.assertTrue("kvs-service" in db)

        # Test with disabled task (should still be 'in' the db)
        task.disabled = True
        self.assertTrue("kvs" in db)

    def test_setitem_add_new_task(self):
        """__setitem__ can add a new task"""
        db = TaskDB()
        task = Task("job-manager")

        # Add via dict-like syntax
        db["job-manager"] = task

        # Verify it was added
        self.assertTrue("job-manager" in db)
        self.assertIs(db.get("job-manager"), task)

    def test_setitem_update_existing_task(self):
        """__setitem__ can update an existing task"""
        db = TaskDB()
        task = Task("sched", priority=100)
        db.add(task)

        # Update the task
        task.priority = 200
        db["sched"] = task

        # Verify update worked
        retrieved = db.get("sched")
        self.assertEqual(retrieved.priority, 200)
        self.assertIs(retrieved, task)

    def test_setitem_with_provides(self):
        """__setitem__ works with tasks that provide services"""
        from flux.modprobe import Module

        db = TaskDB()
        module = Module({"name": "sched-simple", "provides": ["sched", "feasibility"]})

        # Add via setitem
        db["sched-simple"] = module

        # Verify accessible via all names
        self.assertTrue("sched-simple" in db)
        self.assertTrue("sched" in db)
        self.assertTrue("feasibility" in db)
        self.assertIs(db.get("sched"), module)

    def test_setitem_validates_key_matches_name(self):
        """__setitem__ raises ValueError if key doesn't match task.name"""
        db = TaskDB()
        task = Task("kvs")

        # Should raise if key doesn't match
        with self.assertRaises(ValueError) as ctx:
            db["wrong-name"] = task

        self.assertIn("doesn't match task.name", str(ctx.exception))
        self.assertIn("wrong-name", str(ctx.exception))
        self.assertIn("kvs", str(ctx.exception))


class MockContext:
    """Minimal mock context for DependencySolver tests"""

    def __init__(self, rank=0):
        self.rank = rank
        self._attrs = {}
        self._config = {}
        self._env = {}

    def attr_get(self, attr, default=None):
        return self._attrs.get(attr, default)

    def conf_get(self, key, default=None):
        return self._config.get(key, default)

    def getenv(self, var, default=None):
        return self._env.get(var, default)


class TestDependencySolver(unittest.TestCase):
    """Test DependencySolver implementation"""

    def setUp(self):
        """Set up common test fixtures"""
        self.db = TaskDB()
        self.context = MockContext()
        self.solver = DependencySolver(self.db, self.context)

    def test_solve_requirements_basic(self):
        """Basic requirement resolution"""
        task_a = Task("module-a")
        task_b = Task("module-b", requires=["module-a"])
        self.db.add(task_a)
        self.db.add(task_b)

        result = self.solver.solve_requirements(["module-b"])
        # Returns list, not set
        self.assertEqual(set(result), {"module-a", "module-b"})

    def test_solve_requirements_nested(self):
        """Nested requirements (A requires B requires C)"""
        task_a = Task("module-a")
        task_b = Task("module-b", requires=["module-a"])
        task_c = Task("module-c", requires=["module-b"])
        self.db.add(task_a)
        self.db.add(task_b)
        self.db.add(task_c)

        result = self.solver.solve_requirements(["module-c"])
        self.assertEqual(set(result), {"module-a", "module-b", "module-c"})

    def test_solve_requirements_multiple(self):
        """Task with multiple requirements"""
        task_a = Task("module-a")
        task_b = Task("module-b")
        task_c = Task("module-c", requires=["module-a", "module-b"])
        self.db.add(task_a)
        self.db.add(task_b)
        self.db.add(task_c)

        result = self.solver.solve_requirements(["module-c"])
        self.assertEqual(set(result), {"module-a", "module-b", "module-c"})

    def test_solve_requirements_disabled_skipped(self):
        """Disabled tasks are skipped but don't error"""
        task_a = Task("module-a", disabled=True)
        task_b = Task("module-b", requires=["module-a"])
        self.db.add(task_a)
        self.db.add(task_b)

        result = self.solver.solve_requirements(["module-b"])
        # module-a is disabled, so not included
        self.assertEqual(set(result), {"module-b"})

    def test_solve_requirements_circular(self):
        """Circular dependencies don't cause infinite loop"""
        task_a = Task("module-a", requires=["module-b"])
        task_b = Task("module-b", requires=["module-a"])
        self.db.add(task_a)
        self.db.add(task_b)

        result = self.solver.solve_requirements(["module-a"])
        # Both should be included, visited set prevents infinite loop
        self.assertEqual(set(result), {"module-a", "module-b"})

    def test_solve_requirements_nonexistent_raises(self):
        """Nonexistent requirement raises ValueError"""
        task_a = Task("module-a", requires=["nonexistent"])
        self.db.add(task_a)

        with self.assertRaises(ValueError):
            self.solver.solve_requirements(["module-a"])

    def test_solve_requirements_rank_disabled(self):
        """Tasks disabled by rank are skipped"""
        task_a = Task("module-a", ranks="0")
        task_b = Task("module-b", requires=["module-a"])
        self.db.add(task_a)
        self.db.add(task_b)

        # Set context rank to 1, so module-a is disabled
        self.context.rank = 1
        result = self.solver.solve_requirements(["module-b"])
        self.assertEqual(set(result), {"module-b"})

    def test_solve_needs_basic(self):
        """Basic needs satisfaction - no mutation"""
        task_a = Task("module-a")
        task_b = Task("module-b", needs=["module-a"])
        self.db.add(task_a)
        self.db.add(task_b)

        tasks = ["module-a", "module-b"]
        result = self.solver.solve_needs(tasks)
        # Both should remain since module-a provides what module-b needs
        self.assertEqual(set(result), {"module-a", "module-b"})
        # Input should not be mutated
        self.assertEqual(tasks, ["module-a", "module-b"])

    def test_solve_needs_missing_provider(self):
        """Task removed when needed provider missing - no mutation"""
        task_a = Task("module-a", needs=["module-b"])
        self.db.add(task_a)

        tasks = ["module-a"]
        result = self.solver.solve_needs(tasks)
        # module-a removed because module-b is not in tasks
        self.assertEqual(result, [])
        # Input should not be mutated
        self.assertEqual(tasks, ["module-a"])

    def test_solve_needs_disabled_provider(self):
        """Task removed when all providers are disabled"""
        task_a = Task("module-a", disabled=True)
        task_b = Task("module-b", needs=["module-a"])
        self.db.add(task_a)
        self.db.add(task_b)

        tasks = ["module-a", "module-b"]
        result = self.solver.solve_needs(tasks)
        # module-b removed because module-a is disabled
        self.assertEqual(set(result), {"module-a"})

    def test_solve_needs_multiple_providers(self):
        """Needs satisfied if any provider is enabled"""
        task_a = Task("module-a", provides=["service"], disabled=True)
        task_b = Task("module-b", provides=["service"])
        task_c = Task("module-c", needs=["service"])
        self.db.add(task_a)
        self.db.add(task_b)
        self.db.add(task_c)

        tasks = ["module-a", "module-b", "module-c"]
        result = self.solver.solve_needs(tasks)
        # All remain because module-b provides enabled service
        self.assertEqual(set(result), {"module-a", "module-b", "module-c"})

    def test_solve_needs_recursive_removal(self):
        """Removing task cascades to tasks that need it"""
        task_a = Task("module-a")
        task_b = Task("module-b", needs=["module-a"])
        task_c = Task("module-c", needs=["module-b"])
        self.db.add(task_a)
        self.db.add(task_b)
        self.db.add(task_c)

        # Don't include module-a in tasks
        tasks = ["module-b", "module-c"]
        result = self.solver.solve_needs(tasks)
        # Both removed: module-b needs module-a, module-c needs module-b
        self.assertEqual(result, [])

    def test_solve_needs_force_enabled_not_protected(self):
        """Current behavior: force_enabled doesn't prevent needs removal"""
        task_a = Task("module-a", needs=["nonexistent"])
        task_a.force_enabled = True
        self.db.add(task_a)

        tasks = ["module-a"]
        original_tasks = tasks.copy()
        result = self.solver.solve_needs(tasks)
        # Current behavior: still removed despite force_enabled
        self.assertEqual(result, [])
        # Input not mutated
        self.assertEqual(tasks, original_tasks)

    def test_solve_execution_order_basic(self):
        """Basic before/after constraints"""
        task_a = Task("module-a")
        task_b = Task("module-b", after=["module-a"])
        self.db.add(task_a)
        self.db.add(task_b)

        deps = self.solver.solve_execution_order(["module-a", "module-b"])
        # module-b depends on module-a
        self.assertEqual(deps["module-b"], ["module-a"])
        self.assertEqual(deps["module-a"], [])

    def test_solve_execution_order_before(self):
        """Before constraints create reverse dependencies"""
        task_a = Task("module-a", before=["module-b"])
        task_b = Task("module-b")
        self.db.add(task_a)
        self.db.add(task_b)

        deps = self.solver.solve_execution_order(["module-a", "module-b"])
        # module-b depends on module-a (due to before)
        self.assertEqual(deps["module-b"], ["module-a"])
        self.assertEqual(deps["module-a"], [])

    def test_solve_execution_order_wildcard_after(self):
        """after=['*'] means after all other tasks"""
        task_a = Task("module-a")
        task_b = Task("module-b")
        task_c = Task("module-c", after=["*"])
        self.db.add(task_a)
        self.db.add(task_b)
        self.db.add(task_c)

        deps = self.solver.solve_execution_order(["module-a", "module-b", "module-c"])
        # module-c depends on both module-a and module-b
        self.assertIn("module-a", deps["module-c"])
        self.assertIn("module-b", deps["module-c"])

    def test_solve_execution_order_wildcard_before(self):
        """before=['*'] means before all other tasks"""
        task_a = Task("module-a", before=["*"])
        task_b = Task("module-b")
        task_c = Task("module-c")
        self.db.add(task_a)
        self.db.add(task_b)
        self.db.add(task_c)

        deps = self.solver.solve_execution_order(["module-a", "module-b", "module-c"])
        # module-b and module-c both depend on module-a
        self.assertIn("module-a", deps["module-b"])
        self.assertIn("module-a", deps["module-c"])

    def test_solve_execution_order_with_provides(self):
        """Tasks accessible by provides are included in graph"""
        task_a = Task("module-a", provides=["service-x"])
        task_b = Task("module-b", after=["service-x"])
        self.db.add(task_a)
        self.db.add(task_b)

        deps = self.solver.solve_execution_order(["module-a", "module-b"])
        # module-b depends on module-a (resolved via service-x)
        self.assertEqual(deps["module-b"], ["module-a"])

    def test_solve_execution_order_after_absent_task(self):
        """An after edge referencing a task absent from the set is dropped"""
        task_a = Task("module-a")
        task_b = Task("module-b", after=["module-a"])
        self.db.add(task_a)
        self.db.add(task_b)

        # module-a is defined but not in the task set (e.g. a module whose
        # needs-config is unsatisfied), so the edge is ordering-only
        deps = self.solver.solve_execution_order(["module-b"])
        self.assertEqual(deps["module-b"], [])

    def test_solve_execution_order_set_remove(self):
        """set_remove() swaps before/after, reversing execution order"""
        from flux.modprobe import Module

        task_a = Module({"name": "module-a"})
        task_b = Module({"name": "module-b", "after": ["module-a"]})
        self.db.add(task_a)
        self.db.add(task_b)
        task_a.set_remove()
        task_b.set_remove()

        # removal order is the reverse of load order: module-a now
        # depends on module-b
        deps = self.solver.solve_execution_order(["module-a", "module-b"])
        self.assertEqual(deps["module-a"], ["module-b"])
        self.assertEqual(deps["module-b"], [])

    def test_get_requires_basic(self):
        """Basic requires dependency map"""
        task_a = Task("module-a")
        task_b = Task("module-b", requires=["module-a"])
        task_c = Task("module-c", requires=["module-a", "module-b"])
        self.db.add(task_a)
        self.db.add(task_b)
        self.db.add(task_c)

        deps = self.solver.get_requires(["module-a", "module-b", "module-c"])
        self.assertEqual(deps["module-a"], [])
        self.assertEqual(deps["module-b"], ["module-a"])
        self.assertEqual(set(deps["module-c"]), {"module-a", "module-b"})

    def test_get_reverse_requires(self):
        """Reverse requires map shows who requires what"""
        task_a = Task("module-a")
        task_b = Task("module-b", requires=["module-a"])
        task_c = Task("module-c", requires=["module-a"])
        self.db.add(task_a)
        self.db.add(task_b)
        self.db.add(task_c)

        rdeps = self.solver.get_reverse_requires(["module-a", "module-b", "module-c"])
        # module-a is required by module-b and module-c
        self.assertEqual(rdeps["module-a"], {"module-b", "module-c"})

    def test_solve_removal_basic(self):
        """Basic module removal cascades to unused dependencies - no mutation"""
        task_a = Task("module-a")
        task_b = Task("module-b", requires=["module-a"])
        self.db.add(task_a)
        self.db.add(task_b)

        deps = {"module-a": [], "module-b": ["module-a"]}
        modules_to_remove = ["module-b"]
        original_modules = modules_to_remove.copy()
        result = self.solver.solve_removal(deps, modules_to_remove)
        # Both removed: module-b explicitly, module-a cascaded (no longer needed)
        self.assertEqual(set(result), {"module-a", "module-b"})
        # Input not mutated
        self.assertEqual(modules_to_remove, original_modules)

    def test_solve_removal_cascading(self):
        """Removing module cascades to dependencies"""
        task_a = Task("module-a")
        task_b = Task("module-b", requires=["module-a"])
        task_c = Task("module-c", requires=["module-a"])
        self.db.add(task_a)
        self.db.add(task_b)
        self.db.add(task_c)

        deps = {"module-a": [], "module-b": ["module-a"], "module-c": ["module-a"]}
        result = self.solver.solve_removal(deps, ["module-b", "module-c"])
        # All three removed: module-b, module-c, and module-a (no longer needed)
        self.assertEqual(set(result), {"module-a", "module-b", "module-c"})

    def test_solve_removal_with_dependents_raises(self):
        """Cannot remove module with active dependents"""
        task_a = Task("module-a")
        task_b = Task("module-b", requires=["module-a"])
        self.db.add(task_a)
        self.db.add(task_b)

        deps = {"module-a": [], "module-b": ["module-a"]}
        with self.assertRaises(ValueError) as ctx:
            self.solver.solve_removal(deps, ["module-a"])
        self.assertIn("still in use", str(ctx.exception))
        self.assertIn("module-b", str(ctx.exception))

    def test_solve_removal_deep_cascade(self):
        """Deep dependency chain removal"""
        task_a = Task("module-a")
        task_b = Task("module-b", requires=["module-a"])
        task_c = Task("module-c", requires=["module-b"])
        self.db.add(task_a)
        self.db.add(task_b)
        self.db.add(task_c)

        deps = {
            "module-a": [],
            "module-b": ["module-a"],
            "module-c": ["module-b"],
        }
        result = self.solver.solve_removal(deps, ["module-c"])
        # All cascade: module-c -> module-b -> module-a
        self.assertEqual(set(result), {"module-a", "module-b", "module-c"})

    def test_solve_removal_shared_dependency(self):
        """Shared dependency not removed if still needed"""
        task_a = Task("module-a")
        task_b = Task("module-b", requires=["module-a"])
        task_c = Task("module-c", requires=["module-a"])
        self.db.add(task_a)
        self.db.add(task_b)
        self.db.add(task_c)

        deps = {"module-a": [], "module-b": ["module-a"], "module-c": ["module-a"]}
        result = self.solver.solve_removal(deps, ["module-b"])
        # Only module-b removed, module-a still needed by module-c
        self.assertEqual(set(result), {"module-b"})

    def test_solve_removal_missing_module_silent(self):
        """Removing nonexistent module doesn't error (current behavior)"""
        task_a = Task("module-a")
        self.db.add(task_a)

        deps = {"module-a": []}
        # nonexistent-module not in deps, but should not error
        result = self.solver.solve_removal(deps, ["nonexistent-module"])
        self.assertEqual(result, ["nonexistent-module"])

    def test_solve_requirements_ignore_disabled_false(self):
        """Default behavior: disabled tasks are skipped"""
        task_a = Task("module-a", disabled=True)
        task_b = Task("module-b", requires=["module-a"])
        self.db.add(task_a)
        self.db.add(task_b)

        result = self.solver.solve_requirements(["module-b"], ignore_disabled=False)
        # module-a is disabled, should not be included
        self.assertEqual(set(result), {"module-b"})

    def test_solve_requirements_ignore_disabled_true(self):
        """With ignore_disabled=True, disabled tasks are included"""
        task_a = Task("module-a", disabled=True)
        task_b = Task("module-b", requires=["module-a"])
        self.db.add(task_a)
        self.db.add(task_b)

        result = self.solver.solve_requirements(["module-b"], ignore_disabled=True)
        # module-a is disabled but should be included
        self.assertEqual(set(result), {"module-a", "module-b"})

    def test_solve_requirements_ignore_disabled_nested(self):
        """ignore_disabled works recursively through requirements"""
        task_a = Task("module-a", disabled=True)
        task_b = Task("module-b", requires=["module-a"], disabled=True)
        task_c = Task("module-c", requires=["module-b"])
        self.db.add(task_a)
        self.db.add(task_b)
        self.db.add(task_c)

        # Without ignore_disabled
        result1 = self.solver.solve_requirements(["module-c"], ignore_disabled=False)
        self.assertEqual(set(result1), {"module-c"})

        # With ignore_disabled
        result2 = self.solver.solve_requirements(["module-c"], ignore_disabled=True)
        self.assertEqual(set(result2), {"module-a", "module-b", "module-c"})

    def test_solve_requirements_ignore_disabled_directly_requested(self):
        """ignore_disabled includes directly requested disabled module"""
        task_a = Task("module-a", disabled=True)
        self.db.add(task_a)

        # Without ignore_disabled - skipped
        result1 = self.solver.solve_requirements(["module-a"], ignore_disabled=False)
        self.assertEqual(result1, [])

        # With ignore_disabled - included
        result2 = self.solver.solve_requirements(["module-a"], ignore_disabled=True)
        self.assertEqual(result2, ["module-a"])

    def test_resolve_service_prefers_lower_priority_with_satisfied_needs(self):
        """
        resolve_service should select lower-priority module when higher-priority
        module has unsatisfied needs. This is the fluxion scenario.
        """
        # Create a module that high-priority module needs, but don't add it to taskdb
        # (simulating that it's not available)

        # Low priority module with no needs (like sched-simple)
        simple = Task("sched-simple", provides=["sched"], priority=50)

        # High priority module with unmet needs (like sched-fluxion-qmanager)
        fluxion = Task(
            "sched-fluxion-qmanager",
            provides=["sched"],
            needs=["resource"],  # resource module doesn't exist
            priority=500,
        )

        self.db.add(simple)
        self.db.add(fluxion)

        # resolve_service should return simple, not fluxion, because fluxion's needs aren't met
        result = self.solver.resolve_service("sched")
        self.assertEqual(
            result.name,
            "sched-simple",
            "Should select lower-priority sched-simple when fluxion needs not met",
        )

    def test_resolve_service_disabled_alternative_not_in_active_tasks(self):
        """
        When a high-priority alternative is disabled via priority bump but not loaded,
        it should not be attempted to remove during rc3. This is the rc3 shutdown issue.
        """
        # Simulate the scenario:
        # 1. default-module provides "test-service" at priority 100
        # 2. high-priority-alt provides "test-service" at priority 500
        # 3. set_alternative("test-service", "high-priority-alt") bumps priority to 501
        # 4. But high-priority-alt is disabled, so only default-module is loaded
        # 5. During rc3, we should only try to remove default-module, not high-priority-alt

        default = Task("default-module", provides=["test-service"], priority=100)
        high_pri = Task(
            "high-priority-alt", provides=["test-service"], priority=500, disabled=True
        )

        self.db.add(default)
        self.db.add(high_pri)

        # Simulate what happens when user sets alternative but module is disabled
        self.db.set_alternative("test-service", "high-priority-alt")

        # resolve_service should return the enabled module (default-module)
        # even though high-priority-alt has a higher priority
        result = self.solver.resolve_service("test-service")
        self.assertEqual(
            result.name,
            "default-module",
            "Should return enabled module when alternative is disabled",
        )

        # Verify the disabled module is not selected even with its priority bump
        # This prevents it from being added to active_tasks list and attempted removal in rc3
        result2 = self.solver.resolve_service("test-service", ignore_needs=False)
        self.assertEqual(result2.name, "default-module")


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())


# vi: ts=4 sw=4 expandtab
