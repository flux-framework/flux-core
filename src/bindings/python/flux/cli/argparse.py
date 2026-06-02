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


class _FluxArgumentGroup(argparse._ArgumentGroup):
    """ArgumentGroup that supports hidden_aliases in add_argument()."""

    def add_argument(self, *args, hidden_aliases=(), **kwargs):
        if isinstance(hidden_aliases, str):
            raise TypeError(
                "hidden_aliases must be a tuple or list, not a str. "
                "Did you mean hidden_aliases=(%r,)?" % hidden_aliases
            )
        action = super().add_argument(*list(args) + list(hidden_aliases), **kwargs)
        if hidden_aliases:
            action.hidden_option_strings = set(hidden_aliases)
        return action


class _FluxMutuallyExclusiveGroup(argparse._MutuallyExclusiveGroup):
    """MutuallyExclusiveGroup that supports hidden_aliases in add_argument()."""

    def add_argument(self, *args, hidden_aliases=(), **kwargs):
        if isinstance(hidden_aliases, str):
            raise TypeError(
                "hidden_aliases must be a tuple or list, not a str. "
                "Did you mean hidden_aliases=(%r,)?" % hidden_aliases
            )
        action = super().add_argument(*list(args) + list(hidden_aliases), **kwargs)
        if hidden_aliases:
            action.hidden_option_strings = set(hidden_aliases)
        return action


class FluxArgumentParser(argparse.ArgumentParser):
    """ArgumentParser with Flux defaults: allow_abbrev=False, FluxHelpFormatter.

    Additional keyword arguments:
        raw_description (bool): preserve whitespace in description/epilog.
            Silently ignored if formatter_class is passed explicitly.
        argwidth (int): max column for help alignment (default 40).
            Silently ignored if formatter_class is passed explicitly.

    add_argument() accepts an extra keyword argument:
        hidden_aliases (tuple): option strings recognized by the parser but
            suppressed from --help output (e.g. singular forms of plural flags).
            Must be a tuple or list; passing a bare string raises TypeError.
    """

    def __init__(self, *args, raw_description=False, argwidth=40, **kwargs):
        # Lazy import avoids a circular dependency: flux.util re-exports this
        # class while also defining help_formatter, which is needed here.
        from flux.util import help_formatter

        kwargs.setdefault(
            "formatter_class",
            help_formatter(argwidth=argwidth, raw_description=raw_description),
        )
        kwargs.setdefault("allow_abbrev", False)
        super().__init__(*args, **kwargs)

    def add_argument(self, *args, hidden_aliases=(), **kwargs):
        if isinstance(hidden_aliases, str):
            raise TypeError(
                "hidden_aliases must be a tuple or list, not a str. "
                "Did you mean hidden_aliases=(%r,)?" % hidden_aliases
            )
        action = super().add_argument(*list(args) + list(hidden_aliases), **kwargs)
        if hidden_aliases:
            action.hidden_option_strings = set(hidden_aliases)
        return action

    def add_argument_group(self, *args, **kwargs):
        group = _FluxArgumentGroup(self, *args, **kwargs)
        self._action_groups.append(group)
        return group

    def add_mutually_exclusive_group(self, **kwargs):
        group = _FluxMutuallyExclusiveGroup(self, **kwargs)
        self._mutually_exclusive_groups.append(group)
        return group
