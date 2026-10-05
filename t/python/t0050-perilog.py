###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import os
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import textwrap
import time
import unittest

import subflux  # noqa: F401 - To set up PYTHONPATH
from flux.job import JobID
from flux.perilog import (
    UNIT_ENVIRONMENT,
    PerilogProc,
    Terminated,
    check_job_values,
    format_env_line,
    normalize_jobid,
    write_env_file,
)
from pycotap import TAPTestRunner

JOBID_PLAIN = JobID(1234).f58plain

# Run PerilogProc.run() in a child process, since it installs signal
# handlers and the tests need to signal it.
RUN_HARNESS = textwrap.dedent("""
    import sys
    from flux.perilog import PerilogProc
    proc = PerilogProc(
        "prolog",
        sys.argv[1],
        systemctl=sys.argv[2],
        environ={"FLUX_JOB_ID": "1234"},
    )
    sys.exit(proc.run())
    """)

MAIN_HARNESS = textwrap.dedent("""
    import sys
    from flux.perilog import main
    sys.exit(main("prolog", sys.argv[1]))
    """)


def child_environ(**kwargs):
    """Return an environment for a child that can import flux.perilog,
    with no locale variables, as under flux-imp run."""
    env = {
        k: v for k, v in os.environ.items() if not (k == "LANG" or k.startswith("LC_"))
    }
    env["PYTHONPATH"] = os.pathsep.join(sys.path)
    env.update(kwargs)
    return env


def wait_for(path, timeout=30):
    deadline = time.monotonic() + timeout
    while not os.path.exists(path):
        if time.monotonic() > deadline:
            raise TimeoutError(f"timed out waiting for {path}")
        time.sleep(0.05)


class TestNormalizeJobid(unittest.TestCase):
    def test_decimal_converted_to_f58plain(self):
        environ = {"FLUX_JOB_ID": "1234"}
        self.assertEqual(normalize_jobid(environ), JOBID_PLAIN)
        self.assertEqual(environ["FLUX_JOB_ID"], JOBID_PLAIN)

    def test_multibyte_f58_converted_to_f58plain(self):
        environ = {"FLUX_JOB_ID": "ƒ" + JOBID_PLAIN[1:]}
        self.assertEqual(normalize_jobid(environ), JOBID_PLAIN)
        self.assertEqual(environ["FLUX_JOB_ID"], JOBID_PLAIN)

    def test_unset_returns_none_without_adding(self):
        environ = {"PATH": "/usr/bin"}
        self.assertIsNone(normalize_jobid(environ))
        self.assertNotIn("FLUX_JOB_ID", environ)

    def test_empty_returns_none_and_is_preserved(self):
        environ = {"FLUX_JOB_ID": ""}
        self.assertIsNone(normalize_jobid(environ))
        self.assertEqual(environ["FLUX_JOB_ID"], "")

    def test_invalid_raises_valueerror(self):
        with self.assertRaises(ValueError):
            normalize_jobid({"FLUX_JOB_ID": "notajobid"})

    def test_undecodable_raises_valueerror(self):
        with self.assertRaises(ValueError):
            normalize_jobid({"FLUX_JOB_ID": os.fsdecode(b"\xff")})


class TestCheckJobValues(unittest.TestCase):
    def test_job_manager_formats_accepted(self):
        check_job_values({"FLUX_JOB_USERID": "1001", "FLUX_JOB_RANKS": "0-3,5"})
        check_job_values({"FLUX_JOB_RANKS": "7"})
        check_job_values({})

    def test_other_values_rejected(self):
        for name, value in (
            ("FLUX_JOB_USERID", "0 /etc/*"),
            ("FLUX_JOB_USERID", "-1"),
            ("FLUX_JOB_RANKS", "0;id"),
            ("FLUX_JOB_RANKS", "[0-3]"),
            ("FLUX_JOB_RANKS", "0\nX=1"),
        ):
            with self.subTest(name=name, value=value):
                with self.assertRaises(ValueError):
                    check_job_values({name: value})


class TestFormatEnvLine(unittest.TestCase):
    def test_newline_stays_inside_quotes(self):
        # An unescaped newline inside double quotes is literal to systemd,
        # so B=evil must remain part of A's value, not a new assignment.
        self.assertEqual(format_env_line("A", "x\nB=evil"), 'A="x\nB=evil"\n')

    def test_backslash_and_quote_escaped(self):
        self.assertEqual(format_env_line("A", 'a\\b"c'), 'A="a\\\\b\\"c"\n')


class TestWriteEnvFile(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.tmpdir.name, "test.env")

    def tearDown(self):
        self.tmpdir.cleanup()

    def read(self):
        with open(self.path, "rb") as fh:
            return fh.read()

    def mode(self):
        return stat.S_IMODE(os.stat(self.path).st_mode)

    def test_created_with_mode_0600(self):
        old = os.umask(0)
        try:
            write_env_file(self.path, {"A": "1"})
        finally:
            os.umask(old)
        self.assertEqual(self.mode(), 0o600)

    def test_existing_file_mode_reset_and_truncated(self):
        with open(self.path, "w") as fh:
            fh.write("STALE=" + "x" * 1000 + "\n")
        os.chmod(self.path, 0o644)
        write_env_file(self.path, {"A": "1"})
        self.assertEqual(self.mode(), 0o600)
        self.assertEqual(self.read(), b'A="1"\n')

    def test_symlink_replaced_not_followed(self):
        target = os.path.join(self.tmpdir.name, "target")
        with open(target, "w") as fh:
            fh.write("original\n")
        os.symlink(target, self.path)
        write_env_file(self.path, {"A": "1"})
        self.assertFalse(os.path.islink(self.path))
        self.assertEqual(self.read(), b'A="1"\n')
        with open(target) as fh:
            self.assertEqual(fh.read(), "original\n")

    def check_failed_write(self, bad_value, exc):
        with open(self.path, "wb") as fh:
            fh.write(b'OLD="1"\n')
        with self.assertRaises(exc):
            write_env_file(self.path, {"A": "1", "B": bad_value})
        self.assertEqual(self.read(), b'OLD="1"\n')
        self.assertEqual(os.listdir(self.tmpdir.name), ["test.env"])

    def test_failed_write_leaves_existing_file_and_no_temp(self):
        # A lone surrogate cannot be encoded, so the write fails partway.
        self.check_failed_write("\ud800", UnicodeEncodeError)

    def test_signal_during_write_leaves_no_temp(self):
        # A signal handled by run() raises Terminated, a BaseException,
        # at an arbitrary point, including partway through the write.
        class Interrupting(str):
            def replace(self, *args):
                raise Terminated

        self.check_failed_write(Interrupting("x"), Terminated)


class TestPerilogProcNames(unittest.TestCase):
    def test_unit_and_envfile_names(self):
        proc = PerilogProc("epilog", "/run", environ={"FLUX_JOB_ID": "1234"})
        self.assertEqual(proc.unitname, f"flux-epilog@{JOBID_PLAIN}")
        self.assertEqual(proc.envfile, f"/run/flux-epilog@{JOBID_PLAIN}.env")

    def test_unknown_jobid_includes_pid(self):
        proc = PerilogProc("housekeeping", "/run", environ={})
        self.assertEqual(proc.unitname, f"flux-housekeeping@unknown_{os.getpid()}")

    @unittest.skipUnless(shutil.which("systemd-escape"), "requires systemd-escape")
    def test_envfile_matches_unit_template(self):
        # The unit templates name their EnvironmentFile= with %I, the
        # unescaped instance name, so the driver's path must match it.
        for environ in ({"FLUX_JOB_ID": "1234"}, {}):
            with self.subTest(environ=environ):
                proc = PerilogProc("prolog", "/run", environ=environ)
                instance = proc.unitname.split("@", 1)[1]
                unescaped = subprocess.check_output(
                    ["systemd-escape", "--unescape", instance],
                    universal_newlines=True,
                ).strip()
                self.assertEqual(proc.envfile, f"/run/flux-prolog@{unescaped}.env")


class SystemctlTestCase(unittest.TestCase):
    """Base class for tests that run a driver against a mock systemctl.

    The mock appends its arguments to a log file and runs the given shell
    code for start and stop.
    """

    def setUp(self):
        self.tmpdir = tempfile.TemporaryDirectory()
        self.dir = self.tmpdir.name
        self.bindir = os.path.join(self.dir, "bin")
        os.mkdir(self.bindir)
        self.systemctl = os.path.join(self.bindir, "systemctl")
        self.log = os.path.join(self.dir, "log")
        self.unitname = f"flux-prolog@{JOBID_PLAIN}"
        self.envfile = os.path.join(self.dir, f"{self.unitname}.env")

    def tearDown(self):
        self.tmpdir.cleanup()

    def mock_systemctl(self, start="exit 0", stop="exit 0"):
        script = textwrap.dedent(f"""\
            #!/bin/sh
            dir={self.dir}
            echo "$*" >>"$dir/log"
            case "$1" in
            start) {start} ;;
            stop) {stop} ;;
            esac
            """)
        with open(self.systemctl, "w") as fh:
            fh.write(script)
        os.chmod(self.systemctl, 0o755)

    def path(self, name):
        return os.path.join(self.dir, name)

    def invocations(self):
        if not os.path.exists(self.log):
            return []
        with open(self.log) as fh:
            return fh.read().splitlines()

    def start_run(self):
        return subprocess.Popen(
            [sys.executable, "-c", RUN_HARNESS, self.dir, self.systemctl],
            env=child_environ(),
        )


class TestPerilogProcRun(SystemctlTestCase):
    # Long-running mock start that records its pid so that stop can
    # terminate it, as systemctl stop would end a pending start.
    BLOCKING_START = 'echo $$ >"$dir/start.pid"; exec sleep 60'
    KILL_START = 'kill $(cat "$dir/start.pid")'

    def test_envfile_written_before_start(self):
        self.mock_systemctl(start=f'cp "$dir/{self.unitname}.env" "$dir/at-start"')
        proc = self.start_run()
        self.assertEqual(proc.wait(), 0)
        with open(self.path("at-start"), "rb") as fh:
            self.assertEqual(fh.read(), f'FLUX_JOB_ID="{JOBID_PLAIN}"\n'.encode())
        self.assertEqual(self.invocations(), [f"start {self.unitname} --quiet"])

    def test_start_exit_status_returned(self):
        self.mock_systemctl(start="exit 3")
        self.assertEqual(self.start_run().wait(), 3)

    def test_start_killed_by_signal_returns_128_plus_signum(self):
        self.mock_systemctl(start="kill -TERM $$")
        self.assertEqual(self.start_run().wait(), 128 + signal.SIGTERM)

    def check_signal_stops_unit(self, signum):
        self.mock_systemctl(start=self.BLOCKING_START, stop=self.KILL_START)
        proc = self.start_run()
        wait_for(self.path("start.pid"))
        proc.send_signal(signum)
        self.assertEqual(proc.wait(timeout=30), 1)
        self.assertEqual(
            self.invocations(),
            [f"start {self.unitname} --quiet", f"stop {self.unitname}"],
        )

    def test_sigint_stops_unit(self):
        self.check_signal_stops_unit(signal.SIGINT)

    def test_sigterm_stops_unit(self):
        self.check_signal_stops_unit(signal.SIGTERM)

    def test_second_signal_does_not_abort_stop(self):
        stop = (
            'touch "$dir/stopping"; sleep 1; '
            f'{self.KILL_START}; touch "$dir/stopped"'
        )
        self.mock_systemctl(start=self.BLOCKING_START, stop=stop)
        proc = self.start_run()
        wait_for(self.path("start.pid"))
        proc.send_signal(signal.SIGTERM)
        wait_for(self.path("stopping"))
        proc.send_signal(signal.SIGTERM)
        self.assertEqual(proc.wait(timeout=30), 1)
        self.assertTrue(os.path.exists(self.path("stopped")))
        self.assertEqual(
            self.invocations(),
            [f"start {self.unitname} --quiet", f"stop {self.unitname}"],
        )


class TestMain(SystemctlTestCase):
    def run_main(self, runstatedir, **env):
        path = f"{self.bindir}:{os.environ.get('PATH', '/usr/bin:/bin')}"
        return subprocess.run(
            [sys.executable, "-c", MAIN_HARNESS, runstatedir],
            env=child_environ(PATH=path, **env),
            stderr=subprocess.PIPE,
            universal_newlines=True,
        )

    def test_invalid_jobid_fails_without_starting_unit(self):
        self.mock_systemctl()
        result = self.run_main(self.dir, FLUX_JOB_ID="notajobid")
        self.assertEqual(result.returncode, 1)
        self.assertIn("invalid FLUX_JOB_ID", result.stderr)
        self.assertEqual(self.invocations(), [])

    def test_invalid_job_value_fails_without_starting_unit(self):
        self.mock_systemctl()
        result = self.run_main(self.dir, FLUX_JOB_ID="1234", FLUX_JOB_USERID="0;id")
        self.assertEqual(result.returncode, 1)
        self.assertIn("invalid FLUX_JOB_USERID", result.stderr)
        self.assertEqual(self.invocations(), [])

    def test_envfile_failure_fails_without_starting_unit(self):
        self.mock_systemctl()
        missing = os.path.join(self.dir, "nonexistent")
        result = self.run_main(missing, FLUX_JOB_ID="1234")
        self.assertEqual(result.returncode, 1)
        self.assertIn("flux-run-prolog:", result.stderr)
        self.assertEqual(self.invocations(), [])

    def test_unit_receives_only_unit_environment(self):
        # End to end through main(): the env file holds only variables in
        # UNIT_ENVIRONMENT, with FLUX_JOB_ID normalized, and nothing added
        # by the interpreter (e.g. LC_CTYPE from locale coercion).
        self.mock_systemctl()
        result = self.run_main(
            self.dir,
            FLUX_JOB_ID="1234",
            FLUX_CONNECTOR_PATH_PREPEND="/tmp/evil",
            FLUX_URI="local:///tmp/evil",
            LD_PRELOAD="/tmp/evil.so",
            **{"FLUX_X\nLD_PRELOAD": "/tmp/evil.so"},
        )
        self.assertEqual(result.returncode, 0)
        with open(self.envfile, "rb") as fh:
            lines = fh.read().splitlines()
        names = {line.split(b"=", 1)[0].decode() for line in lines}
        self.assertLessEqual(names, set(UNIT_ENVIRONMENT))
        self.assertIn(f'FLUX_JOB_ID="{JOBID_PLAIN}"'.encode(), lines)

    def test_undecodable_value_round_trips(self):
        # Forwarded values that are not validated, e.g. HOME, reach the env
        # file byte for byte, still inside one quoted value.
        self.mock_systemctl()
        env = {os.fsencode(k): os.fsencode(v) for k, v in child_environ().items()}
        env[b"PATH"] = os.fsencode(self.bindir) + b":" + env.get(b"PATH", b"/bin")
        env[b"FLUX_JOB_ID"] = b"1234"
        env[b"HOME"] = b'\xff\nX="1'
        result = subprocess.run([sys.executable, "-c", MAIN_HARNESS, self.dir], env=env)
        self.assertEqual(result.returncode, 0)
        with open(self.envfile, "rb") as fh:
            content = fh.read()
        self.assertIn(b'\nHOME="\xff\nX=\\"1"\n', b"\n" + content)


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())
