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
import shutil
import signal
import tempfile
import textwrap
import unittest
from types import SimpleNamespace

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


class TestApplyOptions(unittest.TestCase):
    def _js(self):
        """Return a fresh single-task jobspec"""
        return JobspecV1.from_command(["hostname"])

    def test_01_env_always_applied(self):
        # environment is always set, even with no env rules
        js = self._js()
        js.apply_options(SimpleNamespace())
        self.assertIsNotNone(js.environment)
        self.assertIn("PATH", js.environment)

    def test_02_env_filter(self):
        js = self._js()
        js.apply_options(SimpleNamespace(env=["-PATH"]))
        self.assertNotIn("PATH", js.environment)

    def test_03_env_expand_shell_option(self):
        js = self._js()
        js.apply_options(SimpleNamespace(env=["MY_ID={{id}}"]))
        opts = js.jobspec["attributes"]["system"]["shell"]["options"]
        self.assertIn("env-expand", opts)
        self.assertEqual(opts["env-expand"]["MY_ID"], "{{id}}")

    def test_04_rlimit_applied(self):
        js = self._js()
        js.apply_options(SimpleNamespace())
        opts = js.jobspec["attributes"]["system"]["shell"]["options"]
        self.assertIn("rlimit", opts)

    def test_05_rlimit_none_propagated(self):
        js = self._js()
        # -* removes all rlimits; no shell options entry should be present
        js.apply_options(SimpleNamespace(rlimit=["-*"]))
        shell = js.jobspec["attributes"]["system"].get("shell", {})
        opts = shell.get("options", {})
        self.assertNotIn("rlimit", opts)

    def test_06_dependency(self):
        js = self._js()
        js.apply_options(SimpleNamespace(dependency=["afterok:12345"]))
        deps = js.jobspec["attributes"]["system"]["dependencies"]
        self.assertEqual(len(deps), 1)
        self.assertEqual(deps[0]["scheme"], "afterok")
        self.assertEqual(deps[0]["value"], "12345")

    def test_07_multiple_dependencies(self):
        js = self._js()
        js.apply_options(SimpleNamespace(dependency=["afterok:1", "afterany:2"]))
        deps = js.jobspec["attributes"]["system"]["dependencies"]
        self.assertEqual(len(deps), 2)

    def test_08_requires_single(self):
        js = self._js()
        js.apply_options(SimpleNamespace(requires=["gpu"]))
        constraints = js.jobspec["attributes"]["system"]["constraints"]
        self.assertIsNotNone(constraints)

    def test_09_requires_multiple_combined(self):
        # Multiple --requires values joined with spaces; MiniConstraintParser
        # combines like "properties" terms into a single list.
        js = self._js()
        js.apply_options(SimpleNamespace(requires=["gpu", "ib"]))
        constraints = js.jobspec["attributes"]["system"]["constraints"]
        self.assertIsNotNone(constraints)
        # Two bare property values combine into {"properties": ["gpu", "ib"]}
        self.assertIn("properties", constraints)
        self.assertIn("gpu", constraints["properties"])
        self.assertIn("ib", constraints["properties"])

    def test_10_setattr_system_prefix(self):
        # key without prefix gets system. prepended
        js = self._js()
        js.apply_options(SimpleNamespace(setattr=["mykey=42"]))
        self.assertEqual(js.jobspec["attributes"]["system"]["mykey"], 42)

    def test_11_setattr_dot_prefix(self):
        # key starting with '.' maps to attributes.
        js = self._js()
        js.apply_options(SimpleNamespace(setattr=[".user.tag=hello"]))
        self.assertEqual(js.jobspec["attributes"]["user"]["tag"], "hello")

    def test_12_setattr_explicit_system_prefix(self):
        js = self._js()
        js.apply_options(SimpleNamespace(setattr=["system.job.name=test"]))
        self.assertEqual(js.jobspec["attributes"]["system"]["job"]["name"], "test")

    def test_13_setopt(self):
        js = self._js()
        js.apply_options(SimpleNamespace(setopt=["verbose=1", "mpi=spectrum"]))
        opts = js.jobspec["attributes"]["system"]["shell"]["options"]
        self.assertEqual(opts["verbose"], 1)
        self.assertEqual(opts["mpi"], "spectrum")

    def test_14_setopt_no_value_defaults_to_1(self):
        js = self._js()
        js.apply_options(SimpleNamespace(setopt=["someflag"]))
        opts = js.jobspec["attributes"]["system"]["shell"]["options"]
        self.assertEqual(opts["someflag"], 1)

    def test_15_signal(self):
        js = self._js()
        js.apply_options(SimpleNamespace(signal="USR1@30s"))
        opts = js.jobspec["attributes"]["system"]["shell"]["options"]
        self.assertIn("signal", opts)
        self.assertEqual(opts["signal"]["signum"], signal.SIGUSR1)

    def test_16_signal_default(self):
        js = self._js()
        js.apply_options(SimpleNamespace(signal="@60s"))
        opts = js.jobspec["attributes"]["system"]["shell"]["options"]
        self.assertIn("signal", opts)
        self.assertEqual(opts["signal"]["signum"], signal.SIGUSR1)

    def test_17_taskmap(self):
        js = self._js()
        js.apply_options(SimpleNamespace(taskmap="block"))
        opts = js.jobspec["attributes"]["system"]["shell"]["options"]
        self.assertIn("taskmap", opts)

    def test_18_add_file_from_data(self):
        # Data with a newline is treated as inline content, not a file path
        js = self._js()
        js.apply_options(SimpleNamespace(add_file=["myfile=line1\nline2"]))
        files = js.jobspec["attributes"]["system"].get("files", {})
        self.assertIn("myfile", files)

    def test_19_add_file_name_missing_raises(self):
        js = self._js()
        with self.assertRaises(ValueError):
            js.apply_options(SimpleNamespace(add_file=["line1\nline2"]))

    def test_20_chaining(self):
        # apply_options() returns self for method chaining
        js = self._js()
        result = js.apply_options(SimpleNamespace())
        self.assertIs(result, js)

    def test_21_prog_none_skips_plugins(self):
        # prog=None should complete without error (no plugin registry needed)
        js = self._js()
        js.apply_options(SimpleNamespace(), prog=None)

    def test_22_missing_attributes_skipped(self):
        # SimpleNamespace with no attributes shouldn't raise
        js = self._js()
        ns = SimpleNamespace()
        js.apply_options(ns, prog=None)
        self.assertNotIn("dependencies", js.jobspec["attributes"]["system"])
        self.assertNotIn("constraints", js.jobspec["attributes"]["system"])

    def test_23_setopt_json_value(self):
        # setopt should JSON-parse values
        js = self._js()
        js.apply_options(SimpleNamespace(setopt=["key=true"]))
        opts = js.jobspec["attributes"]["system"]["shell"]["options"]
        self.assertEqual(opts["key"], True)

    def test_24_setopt_load_from_file(self):
        data = json.dumps({"nested": "value"})
        with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as fp:
            fp.write(data)
            path = fp.name
        try:
            js = self._js()
            js.apply_options(SimpleNamespace(setopt=[f"^myopt={path}"]))
            opts = js.jobspec["attributes"]["system"]["shell"]["options"]
            self.assertEqual(opts["myopt"], {"nested": "value"})
        finally:
            os.unlink(path)

    def test_25_multiple_apply_options_calls(self):
        # shell options accumulate across apply_options calls; when args is
        # passed each time, env is re-applied each call (args is not None path)
        js = self._js()
        js.apply_options(SimpleNamespace(setopt=["a=1"]))
        js.apply_options(SimpleNamespace(setopt=["b=2"]))
        opts = js.jobspec["attributes"]["system"]["shell"]["options"]
        # Both options should be present
        self.assertEqual(opts["a"], 1)
        self.assertEqual(opts["b"], 2)

    def test_26_time_limit_fsd(self):
        # time_limit in FSD string sets duration in seconds
        js = self._js()
        js.apply_options(SimpleNamespace(time_limit="30m"))
        self.assertEqual(js.duration, 1800.0)

    def test_27_time_limit_seconds(self):
        # time_limit as float sets duration directly in seconds
        js = self._js()
        js.apply_options(SimpleNamespace(time_limit=3600.0))
        self.assertEqual(js.duration, 3600.0)

    def test_28_time_limit_none_unchanged(self):
        # time_limit absent leaves duration unchanged
        js = self._js()
        js.apply_options(SimpleNamespace())
        self.assertEqual(js.duration, 0)

    def test_29_kwargs_no_namespace(self):
        # kwargs alone work without any args namespace
        js = self._js()
        js.apply_options(env=["-PATH"], time_limit="1m")
        self.assertNotIn("PATH", js.environment)
        self.assertEqual(js.duration, 60.0)

    def test_30_kwargs_override_namespace(self):
        # kwargs take precedence over namespace attrs of the same name
        js = self._js()
        js.apply_options(SimpleNamespace(time_limit="1h"), time_limit="30m")
        self.assertEqual(js.duration, 1800.0)

    def test_31_shell_options_dict(self):
        # shell_options dict sets shell options without string parsing
        js = self._js()
        js.apply_options(shell_options={"verbose": 1, "pty": 1})
        opts = js.jobspec["attributes"]["system"]["shell"]["options"]
        self.assertEqual(opts["verbose"], 1)
        self.assertEqual(opts["pty"], 1)

    def test_32_shell_options_overrides_setopt(self):
        # shell_options dict applied after setopt strings so kwargs win
        js = self._js()
        js.apply_options(
            SimpleNamespace(setopt=["verbose=0"]),
            shell_options={"verbose": 1},
        )
        opts = js.jobspec["attributes"]["system"]["shell"]["options"]
        self.assertEqual(opts["verbose"], 1)

    def test_33_attributes_dict(self):
        # attributes dict sets jobspec attributes, bare key gets system. prefix
        js = self._js()
        js.apply_options(attributes={"foo": "bar", "system.baz": 42})
        self.assertEqual(js.jobspec["attributes"]["system"]["foo"], "bar")
        self.assertEqual(js.jobspec["attributes"]["system"]["baz"], 42)

    def test_34_attributes_dot_prefix(self):
        # leading '.' in attributes dict key maps to attributes.
        js = self._js()
        js.apply_options(attributes={".user.mykey": "val"})
        self.assertEqual(js.jobspec["attributes"]["user"]["mykey"], "val")

    def test_35_unknown_kwarg_raises(self):
        # unrecognized keyword argument raises TypeError
        js = self._js()
        with self.assertRaises(TypeError):
            js.apply_options(typo_option="bad")

    def test_36_requires_bare_string_normalized(self):
        # A bare string for requires must be treated as a single constraint term,
        # not iterated character-by-character.
        js = self._js()
        js.apply_options(SimpleNamespace(requires="host:node01"))
        constraints = js.jobspec["attributes"]["system"]["constraints"]
        self.assertIsNotNone(constraints)
        # "host:node01" is a hostlist constraint — should appear as a single value
        self.assertIn("hostlist", constraints)
        self.assertEqual(constraints["hostlist"], ["node01"])

    def test_37_dependency_bare_string_normalized(self):
        # A bare string for dependency must produce exactly one dependency entry.
        js = self._js()
        js.apply_options(SimpleNamespace(dependency="afterok:12345"))
        deps = js.jobspec["attributes"]["system"]["dependencies"]
        self.assertEqual(len(deps), 1)
        self.assertEqual(deps[0]["scheme"], "afterok")
        self.assertEqual(deps[0]["value"], "12345")

    def test_38_env_bare_string_normalized(self):
        # A bare string for env must be treated as a single filter rule,
        # not split into individual characters.
        js = self._js()
        js.apply_options(SimpleNamespace(env="-PATH"))
        self.assertNotIn("PATH", js.environment)

    def test_39_rlimit_bare_string_normalized(self):
        # A bare string for rlimit must be treated as a single rule.
        js = self._js()
        js.apply_options(SimpleNamespace(rlimit="-*"))
        shell = js.jobspec["attributes"]["system"].get("shell", {})
        opts = shell.get("options", {})
        self.assertNotIn("rlimit", opts)

    def test_40_env_not_applied_without_args_or_kwarg(self):
        # apply_options() with no args namespace and no env= kwarg must not
        # overwrite a pre-set environment.
        js = self._js()
        js.environment = {"MY_VAR": "42"}
        js.apply_options()
        self.assertEqual(js.environment, {"MY_VAR": "42"})

    def test_41_env_not_reset_on_second_call(self):
        # A second apply_options() call that omits env= must not reset the
        # environment set by the first call.
        js = self._js()
        js.apply_options(env=["-PATH"])
        self.assertNotIn("PATH", js.environment)
        js.apply_options(shell_options={"verbose": 1})
        # PATH was filtered by the first call; second call must not restore it
        self.assertNotIn("PATH", js.environment)


class TestApplyOptionsWithPlugin(unittest.TestCase):
    """Test apply_options() interactions with CLI plugins."""

    PLUGIN_CODE = textwrap.dedent(
        """\
        from flux.cli.plugin import CLIPlugin

        class TestPlugin(CLIPlugin):
            def __init__(self, prog, prefix="test"):
                super().__init__(prog, prefix=prefix)
                self.add_option("--myopt", default="default_val")

            def modify_jobspec(self, args, jobspec):
                if args.myopt is not None:
                    jobspec.setattr_shell_option("test-opt", args.myopt)
    """
    )

    def setUp(self):
        self._plugin_dir = tempfile.mkdtemp()
        with open(os.path.join(self._plugin_dir, "testplugin.py"), "w") as fp:
            fp.write(self.PLUGIN_CODE)
        self._saved_pluginpath = os.environ.get("FLUX_CLI_PLUGINPATH")
        os.environ["FLUX_CLI_PLUGINPATH"] = self._plugin_dir

    def tearDown(self):
        shutil.rmtree(self._plugin_dir)
        if self._saved_pluginpath is None:
            os.environ.pop("FLUX_CLI_PLUGINPATH", None)
        else:
            os.environ["FLUX_CLI_PLUGINPATH"] = self._saved_pluginpath

    def _js(self):
        return JobspecV1.from_command(["hostname"])

    def test_01_modify_jobspec_called(self):
        # plugin modify_jobspec is called; kwarg uses the prefixed dest name.
        # The plugin callback accesses args.myopt (old unprefixed form) which
        # PluginArgsProxy transparently aliases to the prefixed dest.
        js = self._js()
        js.apply_options(prog="submit", test_myopt="hello")
        opts = js.jobspec["attributes"]["system"]["shell"]["options"]
        self.assertEqual(opts["test-opt"], "hello")

    def test_02_prefixed_dest_is_valid_kwarg(self):
        # the prefixed dest (matching the CLI flag) is accepted without TypeError
        js = self._js()
        js.apply_options(prog="submit", test_myopt="x")

    def test_03_unknown_kwarg_raises_with_plugin_loaded(self):
        # unrecognized kwarg raises TypeError even when a plugin is loaded
        js = self._js()
        with self.assertRaises(TypeError):
            js.apply_options(prog="submit", badopt="x")

    def test_04_preinit_seeds_plugin_option_default(self):
        # _preinit=True seeds missing plugin option defaults before modify_jobspec
        js = self._js()
        js.apply_options(prog="submit", _preinit=True)
        opts = js.jobspec["attributes"]["system"]["shell"]["options"]
        self.assertEqual(opts["test-opt"], "default_val")

    def test_05_preinit_called_via_from_submit(self):
        # preinit() is called (default seeding) when using from_submit()
        js = JobspecV1.from_submit(["hostname"])
        opts = js.jobspec["attributes"]["system"]["shell"]["options"]
        self.assertEqual(opts["test-opt"], "default_val")

    def test_07_unprefixed_kwarg_raises_type_error(self):
        # the old unprefixed dest form (myopt=) is no longer accepted;
        # callers must use the prefixed form that matches the CLI flag
        js = self._js()
        with self.assertRaises(TypeError):
            js.apply_options(prog="submit", myopt="x")

    def test_08_proxy_setattr_alias_in_callback(self):
        # a plugin that writes to args via the old unprefixed name in
        # modify_jobspec should update the namespace via the proxy alias
        write_plugin = textwrap.dedent(
            """\
            from flux.cli.plugin import CLIPlugin

            class WritePlugin(CLIPlugin):
                def __init__(self, prog, prefix="wp"):
                    super().__init__(prog, prefix=prefix)
                    self.add_option("--flag", default="original")

                def modify_jobspec(self, args, jobspec):
                    args.flag = "rewritten"  # write via old unprefixed name
                    jobspec.setattr_shell_option("wp-result", args.flag)
            """
        )
        with open(os.path.join(self._plugin_dir, "writeplugin.py"), "w") as fp:
            fp.write(write_plugin)
        js = self._js()
        js.apply_options(prog="submit")
        opts = js.jobspec["attributes"]["system"]["shell"]["options"]
        self.assertEqual(opts["wp-result"], "rewritten")

    def test_09_no_prefix_plugin_kwarg_accepted(self):
        # a plugin with prefix=None has dest equal to the bare option name;
        # the proxy alias map is empty (identity), so args.mykey passes through
        noprefix_code = textwrap.dedent(
            """\
            from flux.cli.plugin import CLIPlugin

            class NoPrefixPlugin(CLIPlugin):
                def __init__(self, prog, prefix=None):
                    super().__init__(prog, prefix=prefix)
                    self.add_option("--mykey")

                def modify_jobspec(self, args, jobspec):
                    if args.mykey:
                        jobspec.setattr_shell_option("np-result", args.mykey)
            """
        )
        with open(os.path.join(self._plugin_dir, "noprefixplugin.py"), "w") as fp:
            fp.write(noprefix_code)
        js = self._js()
        js.apply_options(prog="submit", mykey="bare")
        opts = js.jobspec["attributes"]["system"]["shell"]["options"]
        self.assertEqual(opts["np-result"], "bare")

    def test_10_explicit_dest_plugin_kwarg_accepted(self):
        # a plugin with explicit dest= bypasses alias entirely;
        # the kwarg must use the custom dest name, not the option flag name
        explicit_dest_code = textwrap.dedent(
            """\
            from flux.cli.plugin import CLIPlugin

            class ExplicitDestPlugin(CLIPlugin):
                def __init__(self, prog, prefix="ed"):
                    super().__init__(prog, prefix=prefix)
                    self.add_option("--my-opt", dest="custom_key")

                def modify_jobspec(self, args, jobspec):
                    if args.custom_key:
                        jobspec.setattr_shell_option("ed-result", args.custom_key)
            """
        )
        with open(os.path.join(self._plugin_dir, "explicitdestplugin.py"), "w") as fp:
            fp.write(explicit_dest_code)
        js = self._js()
        js.apply_options(prog="submit", custom_key="val")
        opts = js.jobspec["attributes"]["system"]["shell"]["options"]
        self.assertEqual(opts["ed-result"], "val")

    def test_11_two_plugins_both_fire(self):
        # when two plugins are loaded, both modify_jobspec callbacks run
        second_code = textwrap.dedent(
            """\
            from flux.cli.plugin import CLIPlugin

            class SecondPlugin(CLIPlugin):
                def __init__(self, prog, prefix="b"):
                    super().__init__(prog, prefix=prefix)
                    self.add_option("--bopt")

                def modify_jobspec(self, args, jobspec):
                    jobspec.setattr_shell_option("b-result", "b-fired")
            """
        )
        with open(os.path.join(self._plugin_dir, "secondplugin.py"), "w") as fp:
            fp.write(second_code)
        js = self._js()
        js.apply_options(prog="submit", test_myopt="first")
        opts = js.jobspec["attributes"]["system"]["shell"]["options"]
        self.assertEqual(opts["test-opt"], "first")  # first plugin fired
        self.assertEqual(opts["b-result"], "b-fired")  # second plugin fired

    def test_06_preinit_structural_mutation_ignored_in_from_submit(self):
        # preinit() mutations to structural params (ntasks etc.) have no effect
        # because from_submit() builds the jobspec before apply_options() runs.
        # This test documents the known limitation: the plugin cannot resize
        # the job from preinit() when called through from_submit().
        preinit_code = textwrap.dedent(
            """\
            from flux.cli.plugin import CLIPlugin

            class PreinitPlugin(CLIPlugin):
                def __init__(self, prog, prefix="pre"):
                    super().__init__(prog, prefix=prefix)

                def preinit(self, args):
                    # Attempt to override ntasks — has no effect via from_submit()
                    args.ntasks = 99
            """
        )
        with open(os.path.join(self._plugin_dir, "preinitplugin.py"), "w") as fp:
            fp.write(preinit_code)
        js = JobspecV1.from_submit(["hostname"], ntasks=1)
        slot = js.jobspec["resources"][0]
        # ntasks should still be 1 — preinit mutation was ignored
        self.assertEqual(slot["count"], 1)

if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())
