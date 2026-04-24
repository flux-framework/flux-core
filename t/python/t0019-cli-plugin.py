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
"""
Unit tests for CLIPluginOption dest derivation, PluginArgsProxy, and
CLIPluginRegistry._make_alias().  No running Flux instance is required.

Run with:
    cd t && ../src/cmd/flux python python/t0019-cli-plugin.py
"""
import os
import shutil
import tempfile
import textwrap
import unittest
from types import SimpleNamespace

import subflux  # noqa: F401 - sets up PYTHONPATH
from flux.cli.plugin import (
    CLIPlugin,
    CLIPluginOption,
    CLIPluginRegistry,
    PluginArgsProxy,
)
from pycotap import TAPTestRunner


class TestCLIPluginOption(unittest.TestCase):
    """CLIPluginOption dest derivation."""

    def test_01_auto_dest_uses_prefixed_name(self):
        # dest is derived from the full prefixed CLI flag name
        opt = CLIPluginOption("--my-option", prefix="site")
        self.assertEqual(opt.name, "--site-my-option")
        self.assertEqual(opt.dest, "site_my_option")

    def test_02_unprefixed_dest_stored_for_alias(self):
        # _unprefixed_dest holds the pre-prefix form for proxy aliasing
        opt = CLIPluginOption("--my-option", prefix="site")
        self.assertEqual(opt._unprefixed_dest, "my_option")

    def test_03_auto_dest_no_prefix(self):
        # without a prefix, dest and _unprefixed_dest are identical
        opt = CLIPluginOption("--my-option", prefix=None)
        self.assertEqual(opt.name, "--my-option")
        self.assertEqual(opt.dest, "my_option")
        self.assertEqual(opt._unprefixed_dest, "my_option")

    def test_04_auto_dest_dashes_to_underscores(self):
        # dashes in the prefixed flag name are converted to underscores in dest
        opt = CLIPluginOption("--long-option-name", prefix="site")
        self.assertEqual(opt.dest, "site_long_option_name")
        self.assertEqual(opt._unprefixed_dest, "long_option_name")

    def test_05_explicit_dest_used_as_is(self):
        # explicit dest= is accepted unchanged
        opt = CLIPluginOption("--my-option", prefix="site", dest="custom_dest")
        self.assertEqual(opt.dest, "custom_dest")

    def test_06_explicit_dest_no_alias(self):
        # explicit dest= sets _unprefixed_dest to None — no compat alias needed
        opt = CLIPluginOption("--my-option", prefix="site", dest="custom_dest")
        self.assertIsNone(opt._unprefixed_dest)


class TestPluginArgsProxy(unittest.TestCase):
    """PluginArgsProxy attribute access and mutation."""

    def _proxy(self, alias, **attrs):
        return PluginArgsProxy(SimpleNamespace(**attrs), alias)

    def test_01_getattr_translates_alias(self):
        # reading an old (unprefixed) name returns the value under the new name
        proxy = self._proxy({"old": "new"}, new="value")
        self.assertEqual(proxy.old, "value")

    def test_02_getattr_passthrough_non_alias(self):
        # reading a name not in the alias map passes through to the namespace
        proxy = self._proxy({"old": "new"}, new="x", other="y")
        self.assertEqual(proxy.other, "y")

    def test_03_getattr_new_name_still_accessible(self):
        # the new (prefixed) name is also directly readable
        proxy = self._proxy({"old": "new"}, new="value")
        self.assertEqual(proxy.new, "value")

    def test_04_setattr_translates_alias(self):
        # writing via the old name updates the namespace under the new name
        ns = SimpleNamespace(new=None)
        proxy = PluginArgsProxy(ns, {"old": "new"})
        proxy.old = "written"
        self.assertEqual(ns.new, "written")

    def test_05_setattr_passthrough_non_alias(self):
        # writing a name not in the alias map writes directly to the namespace
        ns = SimpleNamespace(other=None)
        proxy = PluginArgsProxy(ns, {"old": "new"})
        proxy.other = "written"
        self.assertEqual(ns.other, "written")

    def test_06_empty_alias_is_transparent(self):
        # an empty alias map makes the proxy a no-op pass-through
        ns = SimpleNamespace(x=1, y=2)
        proxy = PluginArgsProxy(ns, {})
        self.assertEqual(proxy.x, 1)
        proxy.x = 99
        self.assertEqual(ns.x, 99)


class TestMakeAlias(unittest.TestCase):
    """CLIPluginRegistry._make_alias() builds the right per-plugin alias map."""

    def setUp(self):
        # Empty plugin dir so the registry loads no plugins from disk;
        # plugins are constructed inline in each test.
        self._plugin_dir = tempfile.mkdtemp()
        self._saved = os.environ.get("FLUX_CLI_PLUGINPATH_OVERRIDE")
        os.environ["FLUX_CLI_PLUGINPATH_OVERRIDE"] = self._plugin_dir
        self._registry = CLIPluginRegistry("submit")

    def tearDown(self):
        shutil.rmtree(self._plugin_dir)
        if self._saved is None:
            os.environ.pop("FLUX_CLI_PLUGINPATH_OVERRIDE", None)
        else:
            os.environ["FLUX_CLI_PLUGINPATH_OVERRIDE"] = self._saved

    def test_01_prefixed_plugin_alias_maps_old_to_new(self):
        # a plugin with a prefix gets an alias from unprefixed to prefixed dest
        class P(CLIPlugin):
            def __init__(self, prog, prefix="site"):
                super().__init__(prog, prefix=prefix)
                self.add_option("--my-option")

        alias = self._registry._make_alias(P("submit"))
        self.assertEqual(alias, {"my_option": "site_my_option"})

    def test_02_noprefix_plugin_alias_is_empty(self):
        # a plugin with no prefix has identical prefixed/unprefixed dest names,
        # so the alias map is empty and the proxy is a no-op
        class P(CLIPlugin):
            def __init__(self, prog, prefix=None):
                super().__init__(prog, prefix=prefix)
                self.add_option("--my-option")

        alias = self._registry._make_alias(P("submit"))
        self.assertEqual(alias, {})

    def test_03_explicit_dest_alias_is_empty(self):
        # a plugin with explicit dest= opted out of aliasing (_unprefixed_dest=None)
        class P(CLIPlugin):
            def __init__(self, prog, prefix="site"):
                super().__init__(prog, prefix=prefix)
                self.add_option("--my-option", dest="custom")

        alias = self._registry._make_alias(P("submit"))
        self.assertEqual(alias, {})


class TestConflictDetection(unittest.TestCase):
    """CLIPluginRegistry detects dest conflicts at load time."""

    PLUGIN_SITE = textwrap.dedent(
        """\
        from flux.cli.plugin import CLIPlugin
        class PluginSite(CLIPlugin):
            def __init__(self, prog, prefix="site"):
                super().__init__(prog, prefix=prefix)
                self.add_option("--my-option")
    """
    )

    PLUGIN_VENDOR = textwrap.dedent(
        """\
        from flux.cli.plugin import CLIPlugin
        class PluginVendor(CLIPlugin):
            def __init__(self, prog, prefix="vendor"):
                super().__init__(prog, prefix=prefix)
                self.add_option("--my-option")
    """
    )

    PLUGIN_SITE2 = textwrap.dedent(
        """\
        from flux.cli.plugin import CLIPlugin
        class PluginSite2(CLIPlugin):
            def __init__(self, prog, prefix="site"):
                super().__init__(prog, prefix=prefix)
                self.add_option("--my-option")
    """
    )

    def setUp(self):
        self._saved = os.environ.get("FLUX_CLI_PLUGINPATH_OVERRIDE")

    def tearDown(self):
        if self._saved is None:
            os.environ.pop("FLUX_CLI_PLUGINPATH_OVERRIDE", None)
        else:
            os.environ["FLUX_CLI_PLUGINPATH_OVERRIDE"] = self._saved

    def test_01_same_prefix_same_name_first_wins(self):
        # two plugins with the same prefix and option produce identical dests
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(d, "a.py"), "w") as f:
                f.write(self.PLUGIN_SITE)
            with tempfile.TemporaryDirectory() as d2:
                with open(os.path.join(d2, "b.py"), "w") as f:
                    f.write(self.PLUGIN_SITE2)
                os.environ["FLUX_CLI_PLUGINPATH_OVERRIDE"] = f"{d2}:{d}"
                registry = CLIPluginRegistry("submit")
                self.assertEqual(
                    sum(
                        1
                        for p in registry.plugins
                        if str(p.__class__) == "<class 'b.PluginSite2'>"
                    ),
                    1,
                )

    def test_02_different_prefix_same_name_coexists(self):
        # two plugins with different prefixes get distinct dests and load cleanly
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(d, "a.py"), "w") as f:
                f.write(self.PLUGIN_SITE)
            with open(os.path.join(d, "b.py"), "w") as f:
                f.write(self.PLUGIN_VENDOR)
            os.environ["FLUX_CLI_PLUGINPATH_OVERRIDE"] = d
            registry = CLIPluginRegistry("submit")
            dests = {opt.dest for opt in registry.options}
            self.assertIn("site_my_option", dests)
            self.assertIn("vendor_my_option", dests)


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())
