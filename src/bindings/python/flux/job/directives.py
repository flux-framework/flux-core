###############################################################
# Copyright 2023 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################
import fileinput
import re
import shlex


class Directive:
    """
    This class represents a single job submission directive processed
    from an input file or batch script. Input values to the constructor
    should have the sentinel stripped, and are then split using Python's
    shlex module to provide familiarity UNIX shell syntax and quoting
    for argument. The first argument after shell lexing determines the
    Directive type or "action".

    At this time the only supported actions are "SETARGS", which indicates
    an args list to pass through to the submission utility, and NOOP,
    which is an empty directive.

    If lineno is provided, it is used in error messages to indicate to
    the user more detail about the failing line (e.g. shell parsing error).

    Args:
        value (str): A preprocessed directive with sentinel removed
        lineno (int, optional): The source line number of the current
            directive

    Attributes:
        lineno (int): line number associated with directive
        args (list): list of directive arguments
        action (str): the directive type or instruction to the submission
            utility.

    """

    def __init__(self, value, lineno=-1):
        self.lineno = lineno
        #
        # split value as POSIX shell, removing comments.
        # We specify posix and punctuation_chars to get more predictable
        # quoting and avoid splitting on cmdline args like --foo.
        lexer = shlex.shlex(value, posix=True, punctuation_chars=True)
        #
        # set whitespace_split to match shell parsing of cmdlines as
        # closely as possible as documented in the final note here:
        # https://docs.python.org/3/library/shlex.html
        lexer.whitespace_split = True
        #
        # Add single-quote to escapedquotes. This is necessary to avoid
        # unclosed quote ValueError due to escaped single quote. (The
        # default in Posix mode is to escape only '"')
        lexer.escapedquotes = "\"'"
        try:
            self.args = list(lexer)
        except ValueError as exc:
            raise ValueError(f"line {lineno}: {value}: {exc}") from None

        if not self.args:
            self.action = "NOOP"
        elif self.args[0].startswith("-"):
            self.action = "SETARGS"
        else:
            raise ValueError(f"line {lineno}: Unknown directive: {value}")

    def __str__(self):
        return f"{self.action}({self.args})"


class MultiLine:
    """
    Container for multiline quoted directives.

    A Multiline is opened and closed by matching triple quote at the end
    of a line. While a multi line triple quote is open, lines are pushed
    verbatim (minus any common indent). When the Multiline is finished
    a string is returned with the multiline literal escaped such that it
    may be passed to the Directive constructor.
    """

    def __init__(self):
        self.triplequote = None
        self.inprogress = False
        self.startline = -1
        self.start = None
        self.indent = ""
        self.lines = []

    def finish(self):
        result = self.lines[0] + shlex.quote("\n".join(self.lines[1:]))
        if self.triplequote in result:
            raise ValueError(
                f"improperly terminated triple quoted string at: `{self.start}'"
            )
        self.triplequote = None
        self.lines.clear()
        self.indent = ""
        self.inprogress = False
        self.start = None
        return result

    def append(self, value):
        # Remove common indent and append line
        if value.startswith(self.indent):
            value = value[len(self.indent) :]
        self.lines.append(value)

    def process(self, value, lineno):
        closed = False
        quotes = value[-3:]
        if not self.inprogress:
            # Strip leading whitespace and stash indent.
            # Indent will be removed if matching from all multiline lines
            self.triplequote = quotes
            self.indent = value[: -len(value.lstrip())]
            self.inprogress = True
            self.start = value.lstrip()
            self.startline = lineno
        elif quotes != self.triplequote:
            # A different kind of triplequote, append value and return
            self.append(value)
            return None
        else:
            closed = True

        # Append value with quotes removed:
        self.append(value[:-3])
        if closed:
            return self.finish()
        return None


class DirectiveParser:
    """
    RFC 36 submission directive parser.

    The DirectiveParser looks for sentinels matching the pattern specified
    in RFC 36 in an input stream and extracts each line or quoted multiline
    into a Directive object. Single line strings are split into multiple
    tokens by Python's shlex module, which allows lines to contain familiar
    shell quoting and comments.  As a convenience, inline triple quoting
    is also supported.

    Args:
        inputfile (:obj:`io.TextIOWrapper`):

    Attributes:
        directives (list): list of Directive objects
        script (str): the script to submit
    """

    def __init__(self, inputfile):
        self.directives = []
        self.script = ""
        self.re = re.compile(r"^([^\w]*)((?:flux|FLUX):)(.*)$")
        self.sentinel = None
        self.prefix = None
        self.line = 0

        multiline = MultiLine()
        lineno = 0
        started = False
        directives_disabled = False
        last_directive_line = 0

        for line in inputfile:
            lineno += 1
            self.script += line

            match = self.re.match(line)
            if not match:
                #  All lines in a multiline must start with the sentinel:
                if multiline.inprogress:
                    raise ValueError(
                        f"line {lineno}: unterminated multi-line quote at"
                        + f" line {multiline.startline}: `{multiline.start}'"
                    )
                #  Disable further directives if directives are not already
                #  disabled, line prefix with trailing whitespace removed
                #  doesn't match, and the line is not otherwise empty:
                if (
                    started
                    and not directives_disabled
                    and not line.startswith(self.prefix.rstrip())
                    and line.strip()
                ):
                    directives_disabled = True
                    last_directive_line = lineno - 1
                continue

            #  Get directive prefix and tag:
            started = True
            prefix = match.group(1)
            tag = match.group(2)
            sentinel = prefix + tag

            #  It is an error if a directive is found after directives
            #  have been disabled:
            if directives_disabled:
                #  Raise an error if a directive appears when directives
                #  have been disabled
                raise ValueError(
                    f"line {lineno}: orphan '{tag}' detected: "
                    + f"directives disabled after line {last_directive_line}"
                )

            #  Try processing paired triple quotes on this line.
            #  Raises ValueError on unbalanced triple quotes, or a single
            #  triple quote not at end of line:
            try:
                value = self.triplequote(match.group(3))
            except ValueError as exc:
                raise ValueError(f"line {lineno}: {exc}") from None

            #  If this is the first line with a sentinel, stash the
            #  sentinel for later comparison. Otherwise, raise an error
            #  if it does not match:
            if self.sentinel is None:
                self.sentinel = sentinel
                self.prefix = prefix
            elif sentinel != self.sentinel:
                raise ValueError(
                    f"line {lineno}: sentinel changed from "
                    + f"'{self.sentinel}' to '{sentinel}'"
                )

            if value.endswith('"""') or value.endswith("'''"):
                #  Handle start or end of a multiline triple quoted string:
                result = multiline.process(value, lineno)
                if result:
                    self.append(result, multiline.startline)
            elif multiline.inprogress:
                #  Multiline in progress: collect in multiline object
                multiline.append(value)
            elif value:
                self.append(value, lineno)

    def triplequote(self, value):
        """
        Escape quotes within triple quotes (single or double) so they pass
        unmodified to shell lexing done in Directive constructor
        """

        def paired(n):
            return n > 0 and (n % 2) == 0

        if paired(value.count('"""')) or paired(value.count("'''")):
            value = re.sub(
                r"""((?:["']){3})(.*)\1""", lambda x: shlex.quote(x[2]), value
            )

        elif re.search(r"""((?:["']){3}).""", value):
            #  Multiline strings must have triple quote at end of line,
            #  so a triple quote followed by anything is an error:
            raise ValueError(f"unclosed triple quote: {value.strip()}")
        return value

    def append(self, value, lineno):
        """
        Append one Directive
        """
        self.directives.append(Directive(value, lineno))


if __name__ == "__main__":
    """
    Parse directives from any file for test/debug purposes with
    ::
       flux python -m flux.job.directives FILE
    """
    directives = DirectiveParser(fileinput.input()).directives
    if directives:
        print("\n".join(map(str, directives)))
