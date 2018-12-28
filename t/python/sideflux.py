from __future__ import print_function
import io
import re
import os
import sys
import json
import subprocess
import multiprocessing as mp
import contextlib
import errno
import pprint
import shutil
import tempfile
import time
from six.moves import queue as Queue
import pycotap

# pprint.pprint(os.environ)
flux_exe = ""
if os.environ.get("CHECK_BUILDDIR", None) is not None:
    flux_exe = os.path.abspath(os.environ["CHECK_BUILDDIR"] + "/src/cmd/flux")
else:
    flux_exe = os.path.abspath(
        os.path.dirname(os.path.abspath(__file__)) + "/../../../cmd/flux"
    )


@contextlib.contextmanager
def get_tmpdir():
    d = tempfile.mkdtemp()
    try:
        yield d
    finally:
        shutil.rmtree(d)


def consume(stream):
    while True:
        l = stream.readline()
        if not l:
            break
        sys.stdout.write(l)


class SideFlux(object):
    def __init__(self, size=1):
        global flux_exe
        self.size = size
        self.tmpdir = tempfile.mkdtemp(prefix="flux-sandbox-")
        self.flux_uri = "local://" + self.tmpdir + "/0"
        self.cleaned = False

    def start(self):
        flux_command = [
            flux_exe,
            "start",
            "--bootstrap=selfpmi",
            "--size={}".format(self.size),
            "-o",
            "-Slog-forward-level=7",
            "--scratchdir=" + self.tmpdir,
            "bash",
        ]
        # print ' '.join(flux_command)
        FNULL = open(os.devnull, "w+")
        self.subenv = os.environ.copy()
        self.subenv.pop("FLUX_URI", None)
        self.subenv["TMPDIR"] = self.tmpdir
        self.sub = subprocess.Popen(
            flux_command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            close_fds=True,  # Start a process session to clean up brokers
            preexec_fn=os.setsid,
            env=self.subenv,
        )
        self.sub_in = io.TextIOWrapper(
            self.sub.stdin,
            encoding="utf-8",
            line_buffering=True,  # send data on newline
        )
        self.sub_out = io.TextIOWrapper(self.sub.stdout, encoding="utf-8")
        self.p = mp.Process(target=consume, args=(self.sub_out,))

        print("echo READY", file=self.sub_in)

        self.env_items = {}
        self.env_items["FLUX_URI"] = self.flux_uri

        while True:
            line = self.sub_out.readline()
            if os.environ.get("SIDEFLUX_DEBUG", False):
                print(line)
            if line != "":
                m = re.match(r"\s*(?P<var>[^= ]+)=(?P<val>.*)", line.rstrip())
                if m:
                    if os.environ.get("SIDEFLUX_DEBUG", False):
                        print("setting", m.group("var"), "to", m.group("val"))
                    v = m.group("val")
                    if re.search(r"/\.\./", v):
                        v = os.path.abspath(v)
                    self.env_items[m.group("var")] = v
                if re.search("READY", line):
                    break
                else:
                    if self.sub.poll() is not None:
                        raise EnvironmentError(self.sub.poll())
        self.p.start()

    def get_uri():
        return self.flux_uri

    def apply_environment(self):
        for k, v in self.env_items.items():
            os.environ[k] = v

    def destroy(self):
        if self.cleaned:
            return
        self.cleaned = True
        if os.path.exists(self.tmpdir):
            shutil.rmtree(self.tmpdir)
        try:
            self.sub_in.close()
        except AttributeError:
            pass
        # Kill the process group headed by the subprocess
        os.killpg(self.sub.pid, 15)
        if self.p is not None:
            self.p.terminate()
            self.p.join()

    def run_flux_cmd(self, command=""):
        global flux_exe
        print("{} {}".format(flux_exe, command), file=self.sub_in)

    def run_cmd(self, command=""):
        global flux_exe
        print(command, file=self.sub_in)

    def __del__(self):
        self.destroy()


@contextlib.contextmanager
def run_beside_flux(size=1):
    f = SideFlux(size)
    f.start()
    env = os.environ.copy()
    f.apply_environment()
    # print json.dumps(dict(os.environ))
    try:
        yield f
    finally:
        os.environ.update(env)
        f.destroy()


def apply_wrapper(fun, environment, args, kwargs):
    for k, v in environment.iteritems():
        os.environ[k] = v
    return fun(*args, **kwargs)


class AsyncTimeout(Exception):
    def __init__(self, message):
        super(AsyncTimeout, self).__init__(message)


class SimpleAsyncRunner(object):
    def __init__(self, fun, args, side):
        def q_wrapper(q):
            try:
                res = fun(*args)
                q.put(res)
            except:
                q.put(None)

        self.q = mp.Queue()
        self.p = mp.Process(target=q_wrapper, args=(self.q,))
        self.p.start()
        self.done = False
        self.res = None
        self.side = side

    def get(self, timeout=None):
        """ Get the result, raises AsyncTimeout on timeout failure """
        if not self.done:
            try:
                self.res = self.q.get(True, timeout)
            except Queue.Empty:
                raise AsyncTimeout("The result is not ready, has a test run too long?")
            self.done = True
        return self.res

    def ready(self):
        if not self.q.empty():
            return True
        else:
            return False

    def __del__(self):
        self.p.join()


def apply_under_flux_async(size, fun, args=tuple(), kwargs=dict()):
    f = SideFlux(size)
    f.start()
    result_queue = mp.Queue()
    res = SimpleAsyncRunner(apply_wrapper, (fun, f.env_items, args, kwargs), f)
    return res


def apply_under_flux(size, fun, args=tuple(), kwargs=dict()):
    return apply_under_flux_async(size, fun, args, kwargs).get()


if __name__ == "__main__":
    with run_beside_flux(1) as fp:
        while True:
            pass
