##############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import argparse
import sys

from flux.util import help_formatter


def _add_with_hidden(container, *args, hidden_aliases=(), **kwargs):
    """Call container.add_argument with optional hidden_aliases support."""
    if isinstance(hidden_aliases, str):
        raise TypeError(
            "hidden_aliases must be a tuple or list, not a str. "
            "Did you mean hidden_aliases=(%r,)?" % hidden_aliases
        )
    action = container.add_argument(*args, *hidden_aliases, **kwargs)
    if hidden_aliases:
        action.hidden_option_strings = set(hidden_aliases)
    return action


class _FluxArgumentGroup(argparse._ArgumentGroup):
    """ArgumentGroup that supports hidden_aliases in add_argument()."""

    def add_argument(self, *args, hidden_aliases=(), **kwargs):
        return _add_with_hidden(super(), *args, hidden_aliases=hidden_aliases, **kwargs)


class _FluxMutuallyExclusiveGroup(argparse._MutuallyExclusiveGroup):
    """MutuallyExclusiveGroup that supports hidden_aliases in add_argument()."""

    def add_argument(self, *args, hidden_aliases=(), **kwargs):
        return _add_with_hidden(super(), *args, hidden_aliases=hidden_aliases, **kwargs)


class FluxArgumentParser(argparse.ArgumentParser):
    """ArgumentParser with Flux defaults: allow_abbrev=False, FluxHelpFormatter.

    Additional keyword arguments:
        raw_description (bool): preserve whitespace in description/epilog.
            Silently ignored if formatter_class is passed explicitly.
        argwidth (int): max column for help alignment (default 40).
            Silently ignored if formatter_class is passed explicitly.
        posix (bool): stop consuming options at the first non-option argument
            (POSIX getopt behavior). Intended for submission commands where
            tokens after the job command must not be consumed as Flux options.
            posix=True requires exactly one trailing positional with
            nargs=argparse.REMAINDER and supports no other positionals.
            Clustered short options whose last option takes a separate value
            (e.g. ``-vn 4``) are not supported in posix mode and will error;
            use ``-vn4`` or ``-v -n 4`` instead.

    add_argument() accepts an extra keyword argument:
        hidden_aliases (tuple): option strings recognized by the parser but
            suppressed from --help output (e.g. singular forms of plural flags).
            Must be a tuple or list; passing a bare string raises TypeError.
    """

    def __init__(
        self, *args, posix=False, raw_description=False, argwidth=40, **kwargs
    ):
        kwargs.setdefault(
            "formatter_class",
            help_formatter(argwidth=argwidth, raw_description=raw_description),
        )
        if sys.version_info >= (3, 8):
            # allow_abbrev=False is safe on 3.8+: bpo-26967 moved the gate
            # inside _get_option_tuples so short-option concatenation (-vvv,
            # -n4) still works.
            kwargs.setdefault("allow_abbrev", False)
        # else: leave allow_abbrev at its default (True) so _parse_optional
        # still reaches _get_option_tuples (needed for short-option
        # concatenation on 3.6/3.7); long-option abbreviations are disabled
        # instead by the _get_option_tuples override below (bpo-26967 backport).
        super().__init__(*args, **kwargs)
        self.posix = posix

    if sys.version_info < (3, 8):

        def _get_option_tuples(self, option_string):
            # bpo-26967 backport: reject long-option prefix matching while
            # preserving short-option concatenation handling.
            # On 3.6/3.7, allow_abbrev=False would have gated the entire
            # _get_option_tuples call in _parse_optional, breaking -vvv/-n4.
            # Instead we leave allow_abbrev=True and intercept here: for long
            # options (starting with two prefix chars) return [] to suppress
            # abbreviation matching; for short options fall through to super()
            # so concatenation works normally.
            # 3.6/3.7 are frozen/EOL so this private-method override cannot
            # drift. Verified against the real CPython 3.6 argparse.py.
            chars = self.prefix_chars
            if option_string[0] in chars and option_string[1] in chars:
                return []
            return super()._get_option_tuples(option_string)

    def add_argument(self, *args, hidden_aliases=(), **kwargs):
        return _add_with_hidden(super(), *args, hidden_aliases=hidden_aliases, **kwargs)

    def add_argument_group(self, *args, **kwargs):
        group = _FluxArgumentGroup(self, *args, **kwargs)
        self._action_groups.append(group)
        return group

    def add_mutually_exclusive_group(self, **kwargs):
        group = _FluxMutuallyExclusiveGroup(self, **kwargs)
        self._mutually_exclusive_groups.append(group)
        return group

    def _count_option_args(self, action, remaining):
        """Return number of separate value args this option will consume."""
        nargs = action.nargs
        if nargs is None:
            return 1
        if isinstance(nargs, int):
            return min(nargs, len(remaining))
        if nargs == "?":
            if remaining and self._parse_optional(remaining[0]) is None:
                return 1
            return 0
        if nargs in ("+", "*"):
            # Greedily count consecutive non-option tokens.
            # _parse_optional is portable across 3.6-3.13 when tested via
            # "is None" (all versions return None for non-options, tuple or
            # list for options).
            count = 0
            for tok in remaining:
                if self._parse_optional(tok) is None:
                    count += 1
                else:
                    break
            return count
        return 0  # store_true/false, REMAINDER: no separate value args

    def _split_posix(self, arg_strings):
        """Split arg_strings at the first non-option into (options, positionals).

        Uses self.prefix_chars and self._option_string_actions to classify
        tokens. For nargs='?' options, calls self._parse_optional via
        _count_option_args to check whether the next token is a value or a
        new option; that call is portable across Python 3.6-3.13 because it
        only tests the result for "is None". For nargs='+'/'*', consecutive
        non-option tokens are consumed greedily.

        Negative-number-like tokens (e.g. '-1', '-3.14') are treated as
        non-options when the parser has no negative-number optionals, matching
        standard argparse behavior.

        A user-supplied '--' is preserved as the first element of positionals
        so commands that pass it through to a subprocess (e.g. flux alloc
        passing it to flux broker) work correctly.
        """
        i = 0
        while i < len(arg_strings):
            arg = arg_strings[i]
            if arg == "--":
                return arg_strings[:i], arg_strings[i:]
            if not arg or arg[0] not in self.prefix_chars or len(arg) == 1:
                return arg_strings[:i], arg_strings[i:]
            # Treat negative-number-like tokens as non-options when there are
            # no negative-number optionals, matching stock argparse behavior.
            if (
                self._negative_number_matcher.match(arg)
                and not self._has_negative_number_optionals
            ):
                return arg_strings[:i], arg_strings[i:]
            i += 1
            is_long = arg[1] in self.prefix_chars
            if (is_long and "=" in arg) or (not is_long and len(arg) > 2):
                continue  # value embedded in this token, no next arg consumed
            action = self._option_string_actions.get(arg)
            if action is not None:
                i += self._count_option_args(action, arg_strings[i:])
        return arg_strings, []

    def parse_args(self, args=None, namespace=None):
        if self.posix:
            all_args = list(args) if args is not None else sys.argv[1:]
            options, positionals = self._split_posix(all_args)
            result = super().parse_args(options, namespace)
            # Inject positionals into the REMAINDER dest. argparse sets it to []
            # when no positionals are present; we replace that with the real value,
            # prepending any value that super().parse_args() already put there
            # (e.g. negative numbers that argparse matched as non-options).
            remainder_found = False
            for action in self._actions:
                if action.nargs == argparse.REMAINDER:
                    existing = getattr(result, action.dest, [])
                    if existing:
                        positionals = existing + positionals
                    setattr(result, action.dest, positionals)
                    remainder_found = True
                    break
            if positionals and not remainder_found:
                self.error("unrecognized arguments: %s" % " ".join(positionals))
            return result
        return super().parse_args(args, namespace)
