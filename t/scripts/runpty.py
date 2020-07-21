#!/usr/bin/env python3
"""
Run command in a pty, logging the output one of a set of formats that
is safe and useful for later processing.
"""

import os
import sys
import logging
import pty
import termios
import fcntl
import struct
import time
import json
import argparse

from flux import util
from signal import signal, SIGUSR1, SIGWINCH, SIGTERM, SIGINT


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
        "-c",
        "--quit-char",
        metavar="C",
        help=f"Set the QUIT character (written to pty on SIGUSR1)",
        default="",
    )
    parser.add_argument(
        "--line-buffer", help="Attempt to line buffer theoutput", action="store_true"
    )
    parser.add_argument("COMMAND")
    parser.add_argument("ARGS", nargs=argparse.REMAINDER)
    return parser.parse_args()


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
        except OSError as e:
            self.eof = True

    def get(self):
        data = bytes(self.data)
        self.data = bytearray()
        return data

    def getline(self):
        if not self.data:
            return None
        (line, sep, rest) = self.data.partition(b"\r\n")
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


log = logging.getLogger("runpty")


@util.CLIMain(log)
def main():

    args = parse_args()

    try:
        formatter = formats[args.format]
    except KeyError as e:
        log.error(f'Unknown output format "{args.format}"')
        sys.exit(1)

    (width, height) = map(int, args.window_size.split("x"))
    quit_char = args.quit_char.encode()

    (pid, fd) = pty.fork()

    if pid == pty.CHILD:
        """
        In child
        """
        setwinsize(pty.STDIN_FILENO, height, width)
        os.execvp(args.COMMAND, [args.COMMAND, *args.ARGS])
    else:
        """
        In parent, open log file and read output from child
        """

        signal(SIGWINCH, lambda sig, _: os.kill(pid, sig))
        signal(SIGTERM, lambda sig, _: os.kill(pid, sig))
        signal(SIGINT, lambda sig, _: os.kill(pid, sig))
        signal(SIGUSR1, lambda sig, _: os.write(fd, quit_char))

        ofile = formatter(args.output, width=width, height=height)
        buf = TTYBuffer(fd, linebuffer=args.line_buffer)
        while True:
            buf.read()
            buf.send_data(ofile.write_entry)
            if buf.eof:
                (pid, status) = os.waitpid(pid, 0)
                log.info("child exited with status %s", hex(status))
                sys.exit(status_to_exitcode(status))


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
