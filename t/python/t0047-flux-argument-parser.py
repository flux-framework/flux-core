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

import argparse
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


class TestPosixMode(unittest.TestCase):
    def _make_parser(self, **kwargs):
        p = FluxArgumentParser(prog="flux-run", posix=True, **kwargs)
        p.add_argument("-n", "--ntasks", type=int, default=1)
        p.add_argument("-t", "--time-limit", default=None)
        p.add_argument("command", nargs=argparse.REMAINDER)
        return p

    def test_option_before_command_parsed(self):
        """Flux option before command is consumed by Flux parser"""
        args = self._make_parser().parse_args(["--ntasks=4", "myjob"])
        self.assertEqual(args.ntasks, 4)
        self.assertIn("myjob", args.command)

    def test_option_after_command_not_consumed(self):
        """Option-like token after command NOT consumed by Flux parser"""
        args = self._make_parser().parse_args(["myjob", "--ntasks=8"])
        self.assertEqual(args.ntasks, 1)  # default; --ntasks=8 was not parsed
        self.assertIn("--ntasks=8", args.command)

    def test_flux_option_then_command_then_cmd_option(self):
        """Flux option + command + command option handled correctly"""
        args = self._make_parser().parse_args(["--ntasks=4", "myjob", "--ntasks=8"])
        self.assertEqual(args.ntasks, 4)
        self.assertIn("--ntasks=8", args.command)
        self.assertIn("myjob", args.command)

    def test_explicit_double_dash_preserved(self):
        """User-supplied '--' preserved in REMAINDER for downstream use"""
        args = self._make_parser().parse_args(
            ["--ntasks=4", "--", "myjob", "--ntasks=8"]
        )
        self.assertEqual(args.ntasks, 4)
        # '--' is preserved so commands that pass it to a subprocess
        # (e.g. flux alloc → flux broker) can do so; run/submit strip
        # it in init_jobspec
        self.assertIn("--", args.command)
        self.assertIn("myjob", args.command)
        self.assertIn("--ntasks=8", args.command)

    def test_posix_no_implicit_double_dash(self):
        """POSIX mode does not insert '--' into REMAINDER when none given"""
        args = self._make_parser().parse_args(["--ntasks=4", "myjob", "--ntasks=8"])
        self.assertNotIn("--", args.command)

    def test_short_option_separate_value_skipped(self):
        """Short option with separate value skipped, not positional"""
        args = self._make_parser().parse_args(["-n", "4", "myjob", "--ntasks=8"])
        self.assertEqual(args.ntasks, 4)
        # '4' was the value of -n, not positional
        self.assertNotIn("4", args.command)
        self.assertIn("myjob", args.command)

    def test_short_option_embedded_value_skipped(self):
        """Short option with embedded value (-n4) not positional"""
        args = self._make_parser().parse_args(["-n4", "myjob", "--ntasks=8"])
        self.assertEqual(args.ntasks, 4)
        self.assertNotIn("-n4", args.command)
        self.assertIn("myjob", args.command)

    def test_empty_args(self):
        """Empty arg list returns empty command"""
        p = FluxArgumentParser(prog="test", posix=True)
        p.add_argument("command", nargs=argparse.REMAINDER)
        args = p.parse_args([])
        self.assertEqual(args.command, [])

    def test_posix_false_does_not_preprocess(self):
        """posix=False (default) leaves arg_strings untouched"""
        p = FluxArgumentParser(prog="test")  # posix=False by default
        p.add_argument("--ntasks", type=int, default=1)
        p.add_argument("command", nargs=argparse.REMAINDER)
        # Without posix mode, --ntasks after myjob is still consumed
        # by Flux parser (standard GNU argparse behavior with REMAINDER)
        args = p.parse_args(["myjob", "--ntasks=4"])
        # REMAINDER captures everything from first positional including
        # options
        self.assertIn("myjob", args.command)

    def test_negative_number_in_command(self):
        """Negative number in command position preserved in REMAINDER"""
        # Stock argparse treats -1 as a non-option (negative number),
        # so it lands in REMAINDER. posix mode must match that behavior.
        args = self._make_parser().parse_args(["-1", "myjob"])
        self.assertIn("-1", args.command)
        self.assertIn("myjob", args.command)

    def test_negative_number_terminates_option_processing(self):
        """Negative number terminates option processing in POSIX mode"""
        # In POSIX mode, a negative number acts as a non-option argument
        # that terminates option processing. Subsequent option-like
        # tokens should go into REMAINDER rather than being parsed.
        args = self._make_parser().parse_args(["-1", "--ntasks=8", "myjob"])
        self.assertEqual(args.ntasks, 1)  # default; not parsed
        self.assertIn("-1", args.command)
        self.assertIn("--ntasks=8", args.command)
        self.assertIn("myjob", args.command)

    def test_empty_string_terminates_option_processing(self):
        """Empty string terminates option processing in POSIX mode"""
        # In POSIX mode, an empty string acts as a non-option argument
        # that terminates option processing. Subsequent option-like
        # tokens should go into REMAINDER rather than being parsed.
        args = self._make_parser().parse_args(["", "--ntasks=8", "myjob"])
        self.assertEqual(args.ntasks, 1)  # default; not parsed
        self.assertIn("", args.command)
        self.assertIn("--ntasks=8", args.command)
        self.assertIn("myjob", args.command)

    def test_posix_no_remainder_stray_positionals_error(self):
        """posix parser with no REMAINDER errors on stray positionals"""
        p = FluxArgumentParser(prog="test", posix=True)
        p.add_argument("--foo", default=None)
        with self.assertRaises(SystemExit):
            p.parse_args(["--foo=1", "stray", "args"])

    def test_nargs_plus_posix(self):
        """nargs='+' in posix mode greedily consumes non-option tokens"""
        # With nargs='+', _count_option_args greedily consumes all
        # consecutive non-option tokens, matching stock argparse
        # behavior. Use '--' to delimit the command start explicitly.
        p = FluxArgumentParser(prog="test", posix=True)
        p.add_argument("--multi", nargs="+")
        p.add_argument("command", nargs=argparse.REMAINDER)
        args = p.parse_args(["--multi", "a", "b", "myjob"])
        self.assertEqual(args.multi, ["a", "b", "myjob"])
        self.assertEqual(args.command, [])

    def test_nargs_star_posix(self):
        """nargs='*' in posix mode greedily consumes non-option tokens"""
        p = FluxArgumentParser(prog="test", posix=True)
        p.add_argument("--multi", nargs="*")
        p.add_argument("command", nargs=argparse.REMAINDER)
        args = p.parse_args(["--multi", "a", "b", "myjob"])
        self.assertEqual(args.multi, ["a", "b", "myjob"])
        self.assertEqual(args.command, [])

    def test_nargs_question_posix(self):
        """nargs='?' in posix mode consumes next token if not option"""
        p = FluxArgumentParser(prog="test", posix=True)
        p.add_argument("--file", nargs="?", default=None)
        p.add_argument("--other", default=None)
        p.add_argument("command", nargs=argparse.REMAINDER)
        # When next token is not an option, it's consumed as the value
        args = p.parse_args(["--file", "myfile", "myjob"])
        self.assertEqual(args.file, "myfile")
        self.assertIn("myjob", args.command)
        # When next token looks like an option, it's not consumed
        args = p.parse_args(["--file", "--other=val", "myjob"])
        self.assertIsNone(args.file)
        self.assertEqual(args.other, "val")
        self.assertIn("myjob", args.command)

    def test_nargs_int_posix(self):
        """Integer nargs in posix mode consumes exact number of args"""
        p = FluxArgumentParser(prog="test", posix=True)
        p.add_argument("--coords", nargs=2)
        p.add_argument("command", nargs=argparse.REMAINDER)
        args = p.parse_args(["--coords", "1", "2", "myjob"])
        self.assertEqual(args.coords, ["1", "2"])
        self.assertIn("myjob", args.command)

    def test_long_option_with_equals_posix(self):
        """Long option with embedded = value doesn't consume next token"""
        p = FluxArgumentParser(prog="test", posix=True)
        p.add_argument("--file", default=None)
        p.add_argument("command", nargs=argparse.REMAINDER)
        args = p.parse_args(["--file=test.txt", "myjob"])
        self.assertEqual(args.file, "test.txt")
        self.assertIn("myjob", args.command)

    def test_single_dash_terminates_posix(self):
        """Single dash terminates option processing in POSIX mode"""
        p = FluxArgumentParser(prog="test", posix=True)
        p.add_argument("-n", "--ntasks", type=int, default=1)
        p.add_argument("command", nargs=argparse.REMAINDER)
        # A lone '-' is treated as a non-option argument
        args = p.parse_args(["-", "--ntasks=8", "myjob"])
        self.assertEqual(args.ntasks, 1)  # default; not parsed
        self.assertIn("-", args.command)
        self.assertIn("--ntasks=8", args.command)

    def test_argwidth_parameter(self):
        """argwidth parameter affects help formatting column width"""
        p = FluxArgumentParser(prog="test", argwidth=20)
        p.add_argument("--very-long-option-name", metavar="VALUE", help="test help")
        buf = io.StringIO()
        p.print_help(buf)
        # With narrow argwidth, help text should wrap/indent differently
        # Just verify it doesn't crash and produces output
        self.assertIn("--very-long-option-name", buf.getvalue())

    def test_explicit_formatter_class_overrides(self):
        """Explicit formatter_class overrides raw_description/argwidth"""
        # When formatter_class is explicitly passed, raw_description and
        # argwidth should be silently ignored (they're only used to
        # construct the default)
        p = FluxArgumentParser(
            prog="test",
            formatter_class=argparse.RawDescriptionHelpFormatter,
            raw_description=True,  # should be ignored
            argwidth=10,  # should be ignored
            description="test\n  description",
        )
        buf = io.StringIO()
        p.print_help(buf)
        # Should use RawDescriptionHelpFormatter, preserving whitespace
        self.assertIn("test\n  description", buf.getvalue())


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())
