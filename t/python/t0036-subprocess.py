#!/usr/bin/env python3
###############################################################
# Copyright 2025 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################
#
# flux.subprocess module tests
#
# Tests the Python bindings for RFC 42 subprocess protocol operations:
# - Creating and running background subprocesses
# - Killing subprocesses by PID or label
# - Waiting on subprocess completion
# - Listing active and zombie subprocesses
#

import errno
import os
import signal
import time
import unittest

import flux
import flux.subprocess as subprocess
from flux.constants import FLUX_NODEID_ANY
from subflux import rerun_under_flux


def __flux_size():
    return 2


def wait_for_state_count(
    handle, state="R", count=1, timeout=30, nodeid=FLUX_NODEID_ANY
):
    """
    Wait for the number of processes in state ``state`` to reach ``count`.
    Waits up to ``timeout`` seconds (default 30s)

    Returns:
        True if condition met, False on timeout
    """
    retries = 0
    max_retries = int(timeout * 10)  # 0.1s sleep interval

    while retries < max_retries:
        procs = subprocess.list(handle, nodeid=nodeid).get_processes()
        state_count = sum(1 for proc in procs if proc.state == state)

        if state_count == count:
            return True

        retries += 1
        time.sleep(0.1)

    return False


class TestCommandCreate(unittest.TestCase):
    """Tests for command_create() function"""

    def test_command_create_minimal(self):
        """Test command_create with minimal arguments"""
        cmd = subprocess.command_create(["echo", "hello"])
        self.assertEqual(cmd["cmdline"], ["echo", "hello"])
        self.assertEqual(cmd["cwd"], os.getcwd())
        self.assertIsInstance(cmd["env"], dict)
        self.assertEqual(cmd["opts"], {})
        self.assertEqual(cmd["channels"], [])
        self.assertNotIn("label", cmd)

    def test_command_create_with_label(self):
        """Test command_create with label"""
        cmd = subprocess.command_create(["sleep", "10"], label="test-label")
        self.assertEqual(cmd["label"], "test-label")

    def test_command_create_with_cwd(self):
        """Test command_create with custom cwd"""
        cmd = subprocess.command_create(["ls"], cwd="/tmp")
        self.assertEqual(cmd["cwd"], "/tmp")

    def test_command_create_with_env(self):
        """Test command_create with custom environment"""
        env = {"FOO": "bar", "BAZ": "qux"}
        cmd = subprocess.command_create(["env"], env=env)
        self.assertEqual(cmd["env"], env)

    def test_command_create_with_opts(self):
        """Test command_create with options"""
        opts = {"stdin": False, "pty": True}
        cmd = subprocess.command_create(["cat"], opts=opts)
        self.assertEqual(cmd["opts"], opts)

    def test_command_create_env_isolation(self):
        """Test that default env is a copy, not a reference"""
        cmd1 = subprocess.command_create(["echo"])
        cmd2 = subprocess.command_create(["echo"])
        # Modifying one shouldn't affect the other
        cmd1["env"]["TEST"] = "value"
        self.assertNotIn("TEST", cmd2["env"])


class TestSubprocessRexecBG(unittest.TestCase):
    """Tests for rexec_bg() function"""

    def test_run_minimal(self):
        """Test run with minimal arguments"""
        h = flux.Flux()
        rpc = subprocess.rexec_bg(h, ["true"])
        self.assertIsInstance(rpc, subprocess.SubprocessBackgroundRexecRPC)
        # Get the response to ensure it worked
        result = rpc.get()
        self.assertIn("pid", result)

    def test_run_waitable(self):
        """Test run with waitable flag"""
        h = flux.Flux()
        rpc = subprocess.rexec_bg(h, ["sleep", "0.1"], waitable=True)
        self.assertIsInstance(rpc, subprocess.SubprocessBackgroundRexecRPC)
        result = rpc.get()
        pid = result["pid"]
        # Should be able to wait on it
        wait_rpc = subprocess.wait(h, pid=pid)
        status = wait_rpc.get_status()
        self.assertEqual(status, 0)

    def test_run_with_label(self):
        """Test run with label"""
        h = flux.Flux()
        label = "test-run-label"
        rpc = subprocess.rexec_bg(h, ["sleep", "0.1"], label=label, waitable=True)
        self.assertIsInstance(rpc, subprocess.SubprocessBackgroundRexecRPC)
        rpc.get()
        # Wait by label
        wait_rpc = subprocess.wait(h, label=label)
        status = wait_rpc.get_status()
        self.assertEqual(status, 0)

    def test_run_get_pid_and_rank(self):
        """Test get_pid and get_rank methods"""
        h = flux.Flux()
        rpc = subprocess.rexec_bg(h, ["sleep", "0.1"], waitable=True)
        pid = rpc.get_pid()
        rank = rpc.get_rank()
        self.assertIsInstance(pid, int)
        self.assertGreater(pid, 0)
        self.assertEqual(rank, 0)
        # Clean up
        subprocess.wait(h, pid=pid).get()

    def test_run_get_pid_and_rank_alternate(self):
        """Test get_pid and get_rank methods"""
        h = flux.Flux()
        rpc = subprocess.rexec_bg(h, ["sleep", "0.1"], waitable=True, nodeid=1)
        pid = rpc.get_pid()
        rank = rpc.get_rank()
        self.assertIsInstance(pid, int)
        self.assertGreater(pid, 0)
        self.assertEqual(rank, 1)
        # Clean up
        subprocess.wait(h, pid=pid, nodeid=1).get()

    def test_run_alternate_service(self):
        h = flux.Flux()
        with self.assertRaises(OSError) as ctx:
            subprocess.rexec_bg(h, ["true"], service="invalid").get()
        self.assertIn("No service", str(ctx.exception))


class TestSubprocessKill(unittest.TestCase):
    """Tests for kill() function"""

    def test_kill_with_pid(self):
        """Test kill with pid"""
        h = flux.Flux()
        # Start a long-running process
        rpc = subprocess.rexec_bg(h, ["sleep", "300"], waitable=True)
        pid = rpc.get_pid()
        # Kill it
        kill_rpc = subprocess.kill(h, signum=signal.SIGTERM, pid=pid)
        kill_rpc.get()  # Wait for kill to complete
        # Wait should return signal exit code (128 + 15 = 143)
        wait_rpc = subprocess.wait(h, pid=pid)
        status = wait_rpc.get_status()
        # Check if process was signaled
        self.assertTrue(os.WIFSIGNALED(status))
        self.assertEqual(os.WTERMSIG(status), signal.SIGTERM)

    def test_kill_with_label(self):
        """Test kill with label"""
        h = flux.Flux()
        label = "test-kill-label"
        subprocess.rexec_bg(h, ["sleep", "300"], label=label, waitable=True).get()
        # Kill by label
        kill_rpc = subprocess.kill(h, signum=signal.SIGKILL, label=label)
        kill_rpc.get()
        # Wait for it to exit
        wait_rpc = subprocess.wait(h, label=label)
        status = wait_rpc.get_status()
        self.assertTrue(os.WIFSIGNALED(status))
        self.assertEqual(os.WTERMSIG(status), signal.SIGKILL)

    def test_kill_neither_pid_nor_label(self):
        """Test kill fails when neither pid nor label provided"""
        h = flux.Flux()
        with self.assertRaises(ValueError) as ctx:
            subprocess.kill(h)
        self.assertIn("at least one", str(ctx.exception))

    def test_kill_both_pid_and_label(self):
        """Test kill fails when both pid and label provided"""
        h = flux.Flux()
        with self.assertRaises(ValueError) as ctx:
            subprocess.kill(h, pid=123, label="test")
        self.assertIn("only one", str(ctx.exception))

    def test_kill_nonexistent_pid(self):
        """Test kill with nonexistent pid"""
        h = flux.Flux()
        with self.assertRaises(OSError) as ctx:
            rpc = subprocess.kill(h, pid=999999)
            rpc.get()
        # Should get ESRCH or similar error
        self.assertIn(ctx.exception.errno, [errno.ESRCH, errno.EINVAL])

    def test_kill_default_signum(self):
        """Test kill uses SIGTERM (15) by default"""
        h = flux.Flux()
        rpc = subprocess.rexec_bg(h, ["sleep", "300"], waitable=True)
        pid = rpc.get_pid()
        # Kill with default signal
        kill_rpc = subprocess.kill(h, pid=pid)
        kill_rpc.get()
        # Verify it was SIGTERM
        wait_rpc = subprocess.wait(h, pid=pid)
        status = wait_rpc.get_status()
        self.assertTrue(os.WIFSIGNALED(status))
        self.assertEqual(os.WTERMSIG(status), signal.SIGTERM)

    def test_kill_alternate_service(self):
        h = flux.Flux()
        with self.assertRaises(OSError) as ctx:
            subprocess.kill(h, label="foo", service="invalid").get()
        self.assertIn("No service", str(ctx.exception))


class TestSubprocessWait(unittest.TestCase):
    """Tests for wait() function"""

    def test_wait_with_pid(self):
        """Test wait with pid"""
        h = flux.Flux()
        rpc = subprocess.rexec_bg(h, ["true"], waitable=True)
        pid = rpc.get_pid()
        wait_rpc = subprocess.wait(h, pid=pid)
        self.assertIsInstance(wait_rpc, subprocess.SubprocessWaitRPC)
        status = wait_rpc.get_status()
        self.assertEqual(status, 0)

    def test_wait_with_label(self):
        """Test wait with label"""
        h = flux.Flux()
        label = "test-wait-label"
        subprocess.rexec_bg(h, ["true"], label=label, waitable=True).get()
        wait_rpc = subprocess.wait(h, label=label)
        self.assertIsInstance(wait_rpc, subprocess.SubprocessWaitRPC)
        status = wait_rpc.get_status()
        self.assertEqual(status, 0)

    def test_wait_neither_pid_nor_label(self):
        """Test wait fails when neither pid nor label provided"""
        h = flux.Flux()
        with self.assertRaises(ValueError) as ctx:
            subprocess.wait(h)
        self.assertIn("at least one", str(ctx.exception))

    def test_wait_both_pid_and_label(self):
        """Test wait fails when both pid and label provided"""
        h = flux.Flux()
        with self.assertRaises(ValueError) as ctx:
            subprocess.wait(h, pid=123, label="test")
        self.assertIn("only one", str(ctx.exception))

    def test_wait_nonzero_exit(self):
        """Test wait with non-zero exit code"""
        h = flux.Flux()
        rpc = subprocess.rexec_bg(h, ["sh", "-c", "exit 42"], waitable=True)
        pid = rpc.get_pid()
        wait_rpc = subprocess.wait(h, pid=pid)
        status = wait_rpc.get_status()
        # Check exit status
        self.assertTrue(os.WIFEXITED(status))
        self.assertEqual(os.WEXITSTATUS(status), 42)

    def test_wait_signal_exit(self):
        """Test wait with signal termination"""
        h = flux.Flux()
        rpc = subprocess.rexec_bg(h, ["sleep", "300"], waitable=True)
        pid = rpc.get_pid()
        # Kill it with SIGKILL
        subprocess.kill(h, signum=signal.SIGKILL, pid=pid).get()
        # Wait and check status
        wait_rpc = subprocess.wait(h, pid=pid)
        status = wait_rpc.get_status()
        self.assertTrue(os.WIFSIGNALED(status))
        self.assertEqual(os.WTERMSIG(status), signal.SIGKILL)

    def test_wait_non_waitable(self):
        """Test wait on non-waitable process fails"""
        h = flux.Flux()
        # Start process without waitable flag
        rpc = subprocess.rexec_bg(h, ["sleep", "0.2"], waitable=False)
        pid = rpc.get_pid()
        # Trying to wait should fail
        with self.assertRaises(OSError):
            wait_rpc = subprocess.wait(h, pid=pid)
            wait_rpc.get()

    def test_wait_twice_fails(self):
        """Test waiting on already-reaped process fails"""
        h = flux.Flux()
        subprocess.rexec_bg(h, ["true"], waitable=True, label="wait-twice").get()
        # First wait should succeed
        subprocess.wait(h, label="wait-twice").get()
        # Second wait should fail
        with self.assertRaises(OSError):
            subprocess.wait(h, label="wait-twice").get()

    def test_wait_alternate_service(self):
        h = flux.Flux()
        with self.assertRaises(OSError) as ctx:
            subprocess.wait(h, label="foo", service="invalid").get()
        self.assertIn("No service", str(ctx.exception))


class TestSubprocessList(unittest.TestCase):
    """Tests for list() function"""

    def test_list_empty(self):
        """Test list with no processes"""
        h = flux.Flux()
        rpc = subprocess.list(h, nodeid=0)
        self.assertIsInstance(rpc, subprocess.SubprocessListRPC)
        procs = rpc.get_processes()
        self.assertIsInstance(procs, list)

    def test_list_with_processes(self):
        """Test list with running processes"""
        h = flux.Flux()
        # Start a few processes
        labels = ["proc1", "proc2", "proc3"]
        for label in labels:
            subprocess.rexec_bg(h, ["sleep", "300"], label=label, waitable=True).get()
        # List processes
        rpc = subprocess.list(h)
        procs = rpc.get_processes()
        # Should have at least our 3 processes
        self.assertGreaterEqual(len(procs), 3)
        # Check that they're Subprocess objects
        for proc in procs:
            self.assertIsInstance(proc, subprocess.Subprocess)
            self.assertIsInstance(proc.pid, int)
            self.assertGreater(proc.pid, 0)
            self.assertIn(proc.state, ["R", "Z"])
        # Clean up
        for label in labels:
            subprocess.kill(h, label=label).get()
            subprocess.wait(h, label=label).get()

    def test_list_specific_rank(self):
        """Test list on specific rank"""
        h = flux.Flux()
        # Start process on rank 1
        rpc = subprocess.rexec_bg(h, ["sleep", "300"], nodeid=1, waitable=True)
        pid = rpc.get_pid()
        rank = rpc.get_rank()
        self.assertEqual(rank, 1)
        # List on rank 1
        list_rpc = subprocess.list(h, nodeid=1)
        procs = list_rpc.get_processes()
        # Find our process
        found = False
        for proc in procs:
            if proc.pid == pid:
                found = True
                self.assertEqual(proc.rank, 1)
                break
        self.assertTrue(found, f"Process {pid} not found in list")
        # Clean up
        subprocess.kill(h, nodeid=1, pid=pid).get()
        subprocess.wait(h, nodeid=1, pid=pid).get()

    def test_list_zombie_state(self):
        """Test list shows zombie processes"""
        h = flux.Flux()
        label = "zombie-test"
        # Start a process that exits immediately but is waitable
        subprocess.rexec_bg(h, ["true"], label=label, waitable=True).get()
        # wait for 1 zombie
        self.assertTrue(wait_for_state_count(h, state="Z", count=1))
        # List processes
        rpc = subprocess.list(h)
        procs = rpc.get_processes()
        # Look for our zombie
        found_zombie = False
        for proc in procs:
            if proc.label == label:
                self.assertEqual(proc.state, "Z")
                found_zombie = True
                break
        self.assertTrue(found_zombie, "Zombie process not found")
        # Clean up
        subprocess.wait(h, label=label).get()

    def test_list_alternate_service(self):
        h = flux.Flux()
        with self.assertRaises(OSError) as ctx:
            subprocess.list(h, service="invalid").get()
        self.assertIn("No service", str(ctx.exception))


class TestSubprocessDataclass(unittest.TestCase):
    """Tests for Subprocess dataclass"""

    def test_subprocess_with_label(self):
        """Test Subprocess with label"""
        proc = subprocess.Subprocess(
            pid=123, rank=0, state="R", label="test", cmd="sleep 10"
        )
        self.assertEqual(proc.pid, 123)
        self.assertEqual(proc.rank, 0)
        self.assertEqual(proc.state, "R")
        self.assertEqual(proc.label, "test")
        self.assertEqual(proc.cmd, "sleep 10")

    def test_subprocess_empty_label(self):
        """Test Subprocess with empty label gets replaced with dash"""
        proc = subprocess.Subprocess(
            pid=123, rank=0, state="R", label="", cmd="sleep 10"
        )
        self.assertEqual(proc.label, "-")

    def test_subprocess_none_label(self):
        """Test Subprocess with None label gets replaced with dash"""
        proc = subprocess.Subprocess(
            pid=123, rank=0, state="R", label=None, cmd="sleep 10"
        )
        self.assertEqual(proc.label, "-")

    def test_subprocess_zombie_state(self):
        """Test Subprocess with zombie state"""
        proc = subprocess.Subprocess(
            pid=123, rank=0, state="Z", label="zombie", cmd="sleep 0"
        )
        self.assertEqual(proc.state, "Z")


class TestSubprocessConstants(unittest.TestCase):
    """Tests for module constants"""

    def test_waitable_flag_exists(self):
        """Test that SUBPROCESS_REXEC_WAITABLE constant exists"""
        self.assertTrue(hasattr(subprocess, "SUBPROCESS_REXEC_WAITABLE"))
        self.assertEqual(subprocess.SUBPROCESS_REXEC_WAITABLE, 16)


if __name__ == "__main__":
    if rerun_under_flux(__flux_size()):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner(), buffer=False)
