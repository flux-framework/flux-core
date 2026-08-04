#!/usr/bin/env python3
"""
Run command in a pty, logging the output one of a set of formats that
is safe and useful for later processing.
"""

import argparse
import asyncio
import fcntl
import json
import logging
import os
import pty
import re
import struct
import sys
import termios
import time
from signal import SIGALRM, SIGINT, SIGTERM, SIGUSR1, SIGWINCH, alarm, signal

from flux import util
from flux.job._utils import decode_duration, decode_signal


def setwinsize(fd, rows, cols):
    s = struct.pack("HHHH", rows, cols, 0, 0)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, s)


def getwinsize(fd):
    tsize = os.get_terminal_size()
    return (tsize.lines, tsize.columns)


def status_to_exitcode(status):
    code = 0
    if os.WIFSIGNALED(status):
        code = 128 + os.WTERMSIG(status)
    else:
        code = os.WEXITSTATUS(status)
    return code


class OutputHandler:
    def __init__(self, filename, width=80, height=25):
        self.filename = filename
        self.width = width
        self.height = height
        if self.filename == "-" or self.filename == "stdout":
            self.fp = sys.stdout
        else:
            self.fp = open(filename, "w")

    def format_entry(self, data):
        return data.decode("utf-8", "replace")

    def write_entry(self, data):
        self.fp.write(self.format_entry(data))
        self.fp.flush()


class EventLogOutput(OutputHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        header = dict(
            timestamp=time.time(),
            name="header",
            context=dict(
                version=1, width=self.width, height=self.height, encoding="utf-8"
            ),
        )
        self.fp.write("{}\n".format(json.dumps(header)))
        self.fp.flush()

    def format_entry(self, data):
        entry = dict(
            timestamp=time.time(),
            name="data",
            context=dict(data=data.decode("utf-8", "replace")),
        )
        return "{}\n".format(json.dumps(entry))


class AsciicastOutput(OutputHandler):
    """
    https://github.com/asciinema/asciinema/blob/develop/doc/asciicast-v2.md
    """

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.t0 = time.time()
        ts = int(self.t0)
        header = dict(version=2, width=self.width, height=self.height, timestamp=ts)
        self.fp.write("{}\n".format(json.dumps(header)))
        self.fp.flush()

    def format_entry(self, data):
        dt = time.time() - self.t0
        entry = [dt, "o", data.decode("utf-8", "replace")]
        return "{}\n".format(json.dumps(entry))


formats = {
    "raw": OutputHandler,
    "asciicast": AsciicastOutput,
    "eventlog": EventLogOutput,
}


def parse_args():
    try:
        ws_default = "{0.columns}x{0.lines}".format(os.get_terminal_size())
    except OSError:
        ws_default = "80x25"

    format_list = ",".join(formats.keys())

    parser = argparse.ArgumentParser(
        description="run command with a pty, log output to a file",
        formatter_class=util.help_formatter(),
    )
    parser.add_argument(
        "-o", "--output", help="set output file. Default=stdout", default="-"
    )
    parser.add_argument(
        "-n", "--no-output", help="redirect output to /dev/null", action="store_true"
    )
    parser.add_argument(
        "-i",
        "--input",
        help="set an input file in asciicast format. "
        + "Use the special value 'none' to close stdin of pty immediately.",
    )
    parser.add_argument("--expect", help="set an expected output file")
    parser.add_argument("--stderr", help="redirect stderr of process")
    parser.add_argument(
        "-f",
        "--format",
        help=f"set output format ({format_list}). Default=raw",
        default="raw",
    )
    parser.add_argument(
        "-w",
        "--window-size",
        metavar="WxH",
        help=f"set pty window size in WIDTHxHEIGHT (default is {ws_default})",
        default=ws_default,
    )
    parser.add_argument(
        "--term",
        metavar="TERMINAL",
        help="set value of TERM variable for client (default xterm)",
        default="xterm",
    )
    parser.add_argument(
        "-c",
        "--quit-char",
        metavar="C",
        help="Set the QUIT character (written to pty on SIGUSR1)",
        default="",
    )
    parser.add_argument(
        "--line-buffer", help="Attempt to line buffer the output", action="store_true"
    )
    parser.add_argument(
        "-t",
        "--timeout",
        metavar="[SIGNAL@]TIME",
        help="Set a timeout (and optional SIGNAL) for command",
    )
    parser.add_argument("COMMAND")
    parser.add_argument("ARGS", nargs=argparse.REMAINDER)
    return parser.parse_args()


class ExpectEntry:
    """
    A single Expect/Send entry with optional timeout

    An entry has the form

        {"expect":s, "send":s, "timeout"?i, "exit"?b}
        or
        {"expect":s, "command":s, "timeout"?i, "exit"?b}

    Where 'expect' is a pattern, 'send' is the string to send as input
    after the expected pattern matches, 'command' is a shell command to
    execute when the pattern matches, 'exit' is an optional boolean that
    causes runpty to exit successfully after the match, and 'timeout' is
    an optional integer number of seconds after which the pattern match
    times out.

    Either 'send' or 'command' may be specified, but not both.
    """

    def __init__(self, entry):
        self.expect = re.compile(entry["expect"])
        self.send = entry.get("send")
        self.command = entry.get("command")
        self.exit = entry.get("exit", False)
        self.timeout = int(entry.get("timeout", 60.0))
        if self.send and self.command:
            raise ValueError("Cannot specify both 'send' and 'command'")

    def __str__(self):
        return self.expect.pattern


class Expecter:
    """
    Class which represents a list of expected output patterns along with
    responses that are input after a pattern match. Responses are popped
    off the stack as they are used.
    """

    def __init__(self):
        self.entries = []
        self.current = None
        self.data = ""

    def add_file(self, input_file):
        """
        Add a set of expect/send entries from a JSON file. The file should
        contain a JSON array of ExpectEntry objects.
        """
        for entry in json.load(input_file):
            self.entries.append(ExpectEntry(entry))
        self.next()

    def next(self, data=""):
        """
        Advance to the next expect entry
        """
        if self.entries:
            self.current = self.entries.pop(0)
            self.data = data
            alarm(self.current.timeout)
        else:
            self.current = None
            self.data = ""
            alarm(0)

    def match(self, data):
        """
        Accumulate data in the expect match buffer and return True if
        the currently active pattern matches the buffer.
        """
        if self.current and data:
            self.data += data
            if re.search(self.current.expect, self.data):
                return True
        return False

    def pop(self):
        """
        Return the current entry after a match and advance to the
        next expected pattern. Returns a tuple of (send_data, command, exit)
        where send_data is bytes to write to pty (or None), command
        is a shell command to execute (or None), and exit is a boolean
        indicating whether runpty should exit successfully after this match.
        """
        if self.current:
            send_data = self.current.send.encode("utf-8") if self.current.send else None
            command = self.current.command
            should_exit = self.current.exit
            self.next()
            return (send_data, command, should_exit)
        return (None, None, False)


class TTYBuffer:
    def __init__(self, fd, linebuffer=False, bufsize=1024):
        self.linebuffered = linebuffer
        self.bufsize = bufsize
        self.fd = fd
        self.eof = False
        self.data = bytearray()

    def setlinebuf(self):
        self.linebuffered = True

    def read(self):
        try:
            data = os.read(self.fd, self.bufsize)
            self.data += data
        except (BlockingIOError, InterruptedError):
            pass
        except OSError:
            self.eof = True

    def peek(self):
        return self.data.decode("utf-8", errors="surrogateescape")

    def get(self):
        data = bytes(self.data)
        self.data = bytearray()
        return data

    def getline(self):
        if not self.data:
            return None
        line, sep, rest = self.data.partition(b"\r\n")
        if sep:
            self.data = rest
            return line + sep

    def send_data(self, writer):
        if self.linebuffered:
            line = self.getline()
            while line:
                writer(line)
                line = self.getline()
            if self.eof:
                writer(self.get())
        else:
            writer(self.get())


def parse_timeout_arg(arg):
    """
    Parse --timeout=[SIG@]TIME option
    """
    sig = SIGTERM
    timestr = arg
    if "@" in arg:
        # SIG@TIME format:
        sig, _, timestr = arg.partition("@")
        sig = decode_signal(sig)

    time = decode_duration(timestr)

    # Add unit to timestr if missing:
    if timestr[-1].isdigit() or timestr[-1] == ".":
        timestr += "s"

    return sig, time, timestr


log = logging.getLogger("runpty")


@util.CLIMain(log)
def main():

    # Avoid asyncio DEBUG log messages (why is this on by default??)
    logging.getLogger("asyncio").setLevel(logging.WARNING)

    sys.stdout = open(
        sys.stdout.fileno(), "w", encoding="utf8", errors="surrogateescape"
    )
    sys.stderr = open(
        sys.stderr.fileno(), "w", encoding="utf8", errors="surrogateescape"
    )

    timeout_sec = None
    timeout_arg = None
    timeout_sig = SIGTERM

    args = parse_args()
    if args.no_output and args.output != "-":
        log.error("Do not specify --no-output and --output")
        sys.exit(1)
    if args.no_output:
        args.output = "/dev/null"
    if args.timeout:
        timeout_sig, timeout_sec, timeout_arg = parse_timeout_arg(args.timeout)

    try:
        formatter = formats[args.format]
    except KeyError:
        log.error(f'Unknown output format "{args.format}"')
        sys.exit(1)

    width, height = map(int, args.window_size.split("x"))
    quit_char = args.quit_char.encode()

    pid, fd = pty.fork()

    if pid == pty.CHILD:
        """
        In child
        """
        if args.stderr:
            sys.stderr = open(args.stderr, "w")
            os.dup2(sys.stderr.fileno(), 2)

        os.environ["TERM"] = args.term
        setwinsize(pty.STDIN_FILENO, height, width)
        os.execvp(args.COMMAND, [args.COMMAND, *args.ARGS])
    else:
        """
        In parent, open log file and read output from child
        """
        os.set_blocking(fd, False)

        signal(SIGWINCH, lambda sig, _: os.kill(pid, sig))
        signal(SIGTERM, lambda sig, _: os.kill(pid, sig))
        signal(SIGINT, lambda sig, _: os.kill(pid, sig))
        signal(SIGUSR1, lambda sig, _: os.write(fd, quit_char))

        ofile = formatter(args.output, width=width, height=height)
        buf = TTYBuffer(fd, linebuffer=args.line_buffer)

        loop = asyncio.new_event_loop()

        if args.input and args.input == "none":

            def write_eof(fd):
                if hasattr(termios, "CEOF"):
                    value = bytes([termios.CEOF])
                else:
                    # No CEOF in termios, assume Ctrl-D/EOT (0x4)
                    value = bytes([4])
                os.write(fd, value)

            #  Sometimes the shell (if that is the target of runpty)
            #   does not read EOF if it is sent too soon. Therefore send
            #   EOF control character now, then 3 extra times to ensure it is
            #   read eventually.
            #
            write_eof(fd)
            loop.call_later(0.1, write_eof, fd)
            loop.call_later(0.5, write_eof, fd)
            loop.call_later(1.0, write_eof, fd)
            loop.call_later(15, write_eof, fd)

        elif args.input:

            def write_tty(s):
                os.write(fd, s.encode("utf-8"))

            with open(args.input, "r") as infile:
                infile.readline()
                for line in infile:
                    timestamp, event_type, data = json.loads(line)
                    if event_type == "i":
                        loop.call_later(float(timestamp), write_tty, data)

        expect = Expecter()
        if args.expect:
            with open(args.expect) as fp:
                expect.add_file(fp)

        def timeout():
            os.kill(pid, timeout_sig)
            if args.expect:
                log.error("timeout waiting for pattern '%s'", expect.current)
            else:
                log.error("command '%s' timed out after %s", args.COMMAND, timeout_arg)

        def sigalrm_handler(sig, _):
            timeout()

        signal(SIGALRM, sigalrm_handler)

        # Set when an expect entry with "exit": true matches, so that
        # runpty exits successfully after the reactor stops regardless of
        # the child's exit status.
        exit_requested = False

        def read_tty():
            nonlocal exit_requested
            buf.read()
            if expect.match(buf.peek()):
                send_data, command, should_exit = expect.pop()
                if send_data:
                    os.write(fd, send_data)
                if command:
                    os.system(command)
                if should_exit:
                    # Pattern matched and exit requested - terminate child
                    # and exit successfully regardless of child exit status.
                    # Stop the reactor deterministically here rather than
                    # racing a delayed exit against the pty EOF that results
                    # from killing the child (which would otherwise fall
                    # through to the child's exit status below).
                    try:
                        os.kill(pid, SIGTERM)
                    except ProcessLookupError:
                        pass
                    exit_requested = True
                    loop.remove_reader(fd)
                    loop.stop()
                    return
            buf.send_data(ofile.write_entry)
            if buf.eof:
                loop.stop()

        # Handle overall timeout
        if timeout_sec is not None:
            loop.call_later(timeout_sec, timeout)

        loop.add_reader(fd, read_tty)
        loop.run_forever()

        # Exit successfully if an expect entry requested it, without
        # waiting on (or reporting) the child's exit status.
        if exit_requested:
            sys.exit(0)

        pid, status = os.waitpid(pid, 0)
        sys.exit(status_to_exitcode(status))


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
