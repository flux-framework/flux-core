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

import io
import unittest

import subflux  # noqa: F401 - To set up PYTHONPATH
from flux.cli.argparse import FluxArgumentParser
from pycotap import TAPTestRunner


class TestFluxArgumentParser(unittest.TestCase):
    def test_allow_abbrev_false(self):
        """Abbreviated options are rejected"""
        p = FluxArgumentParser(prog="test")
        p.add_argument("--foo-bar")
        with self.assertRaises(SystemExit):
            p.parse_args(["--foo"])

    def test_exact_option_still_works(self):
        """Full option names are accepted"""
        p = FluxArgumentParser(prog="test")
        p.add_argument("--foo-bar", default="x")
        args = p.parse_args(["--foo-bar=y"])
        self.assertEqual(args.foo_bar, "y")

    def test_default_formatter_is_flux(self):
        """Default formatter produces Flux-style help (=, wide columns)"""
        p = FluxArgumentParser(prog="test")
        p.add_argument("--my-option", metavar="VAL", help="test")
        buf = io.StringIO()
        p.print_help(buf)
        self.assertIn("--my-option=VAL", buf.getvalue())

    def test_raw_description(self):
        """raw_description=True preserves whitespace in description"""
        p = FluxArgumentParser(prog="test", raw_description=True, description="a\n  b")
        buf = io.StringIO()
        p.print_help(buf)
        self.assertIn("a\n  b", buf.getvalue())

    def test_subparsers_inherit_flux_parser(self):
        """Subparsers via add_subparsers() are also FluxArgumentParser"""
        p = FluxArgumentParser(prog="test")
        subs = p.add_subparsers()
        sub = subs.add_parser("cmd")
        self.assertIsInstance(sub, FluxArgumentParser)

    def test_subparser_allow_abbrev_false(self):
        """Subparsers also reject abbreviated options"""
        p = FluxArgumentParser(prog="test")
        subs = p.add_subparsers(dest="cmd")
        sub = subs.add_parser("run")
        sub.add_argument("--foo-bar")
        with self.assertRaises(SystemExit):
            p.parse_args(["run", "--foo"])


class TestHiddenAliases(unittest.TestCase):
    def _make_parser(self):
        p = FluxArgumentParser(prog="test")
        p.add_argument(
            "--states",
            hidden_aliases=("--state",),
            metavar="STATE,...",
            help="Output resources in given states",
        )
        return p

    def test_hidden_alias_recognized(self):
        """Hidden alias is accepted by the parser"""
        args = self._make_parser().parse_args(["--state=free"])
        self.assertEqual(args.states, "free")

    def test_primary_option_still_works(self):
        """Primary option name is unaffected"""
        args = self._make_parser().parse_args(["--states=free,down"])
        self.assertEqual(args.states, "free,down")

    def test_hidden_alias_absent_from_help(self):
        """Hidden alias does not appear in --help output"""
        buf = io.StringIO()
        self._make_parser().print_help(buf)
        help_text = buf.getvalue()
        self.assertIn("--states", help_text)
        # --state must not appear as a standalone option string
        self.assertNotIn("--state,", help_text)
        self.assertNotIn("--state=", help_text)

    def test_hidden_alias_on_append_action(self):
        """hidden_aliases works with action='append'"""
        p = FluxArgumentParser(prog="test")
        p.add_argument("--plugins", hidden_aliases=("--plugin",), action="append")
        args = p.parse_args(["--plugin=foo", "--plugins=bar"])
        self.assertEqual(args.plugins, ["foo", "bar"])

    def test_hidden_alias_on_argument_group(self):
        """hidden_aliases works when add_argument called on argument group"""
        p = FluxArgumentParser(prog="test")
        grp = p.add_argument_group("Options")
        grp.add_argument(
            "--plugins",
            hidden_aliases=("--plugin",),
            action="append",
            default=[],
        )
        args = p.parse_args(["--plugin=foo"])
        self.assertEqual(args.plugins, ["foo"])

    def test_hidden_alias_group_absent_from_help(self):
        """Hidden alias added via argument group is absent from --help"""
        p = FluxArgumentParser(prog="test")
        grp = p.add_argument_group("Options")
        grp.add_argument(
            "--plugins",
            hidden_aliases=("--plugin",),
            action="append",
            default=[],
        )
        buf = io.StringIO()
        p.print_help(buf)
        help_text = buf.getvalue()
        self.assertIn("--plugins", help_text)
        self.assertNotIn("--plugin,", help_text)
        self.assertNotIn("--plugin=", help_text)

    def test_multiple_hidden_aliases(self):
        """Multiple hidden aliases can be specified"""
        p = FluxArgumentParser(prog="test")
        p.add_argument("--states", hidden_aliases=("--state", "--st"), default=None)
        self.assertEqual(p.parse_args(["--state=x"]).states, "x")
        self.assertEqual(p.parse_args(["--st=x"]).states, "x")
        self.assertEqual(p.parse_args(["--states=x"]).states, "x")

    def test_subparser_hidden_alias(self):
        """hidden_aliases work in subparsers"""
        p = FluxArgumentParser(prog="test")
        subs = p.add_subparsers(dest="cmd")
        sub = subs.add_parser("list")
        sub.add_argument("--states", hidden_aliases=("--state",), default=None)
        args = p.parse_args(["list", "--state=free"])
        self.assertEqual(args.states, "free")

    def test_hidden_alias_on_mutually_exclusive_group(self):
        """hidden_aliases works in a mutually exclusive group"""
        p = FluxArgumentParser(prog="test")
        grp = p.add_mutually_exclusive_group()
        grp.add_argument(
            "--states",
            hidden_aliases=("--state",),
            default=None,
        )
        args = p.parse_args(["--state=free"])
        self.assertEqual(args.states, "free")

    def test_hidden_alias_mutex_group_absent_from_help(self):
        """Hidden alias in mutually exclusive group is absent from --help"""
        p = FluxArgumentParser(prog="test")
        grp = p.add_mutually_exclusive_group()
        grp.add_argument(
            "--states",
            hidden_aliases=("--state",),
            default=None,
        )
        buf = io.StringIO()
        p.print_help(buf)
        help_text = buf.getvalue()
        self.assertNotIn("--state,", help_text)
        self.assertNotIn("--state=", help_text)

    def test_hidden_aliases_str_raises_type_error(self):
        """Passing a bare string as hidden_aliases raises TypeError"""
        p = FluxArgumentParser(prog="test")
        with self.assertRaises(TypeError):
            p.add_argument("--states", hidden_aliases="--state")


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())
