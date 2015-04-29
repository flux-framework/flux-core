from __future__ import print_function
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

# pprint.pprint(os.environ)
flux_exe = ''
if os.environ.get('CHECK_BUILDDIR', None) is not None:
  flux_exe = os.path.abspath(os.environ['CHECK_BUILDDIR'] + '/src/cmd/flux')
else:
  flux_exe = os.path.abspath(os.path.dirname(os.path.abspath(__file__)) + '/../../../cmd/flux')

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
    flux_command = [flux_exe,
                    '--verbose',
                    'start',
                    '--size={}'.format(size),
                    '-o',
                    '--verbose,-L,stderr',
                    'bash']
                    # """bash -c 'echo READY ; while true ; do sleep 1; done' """]
    # print ' '.join(flux_command)
    FNULL = open(os.devnull, 'w+')

    self.sub = subprocess.Popen(flux_command,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        close_fds=True,
        preexec_fn=os.setsid,# Start a process session to clean up brokers
        )
    print('echo READY', file=self.sub.stdin)
    while True:
      line = self.sub.stdout.readline()
      # print line
      if line != '':
        m = re.match(r"\s+(FLUX_[^=]+)=(.*)", line.rstrip())
        if m:
          print("setting", m.group(1), "to", os.path.abspath(m.group(2)))
          os.environ[m.group(1)] = os.path.abspath(m.group(2))
        m = re.match(r'lt-flux-broker: FLUX_TMPDIR: (.*)', line.rstrip())
        if m:
          print("setting", "FLUX_TMPDIR", "to", os.path.abspath(m.group(1)))
          os.environ['FLUX_TMPDIR'] = m.group(1)
        if re.search('READY', line):
          break
      else:
        break
    self.p = mp.Process(target=consume, args=(self.sub.stdout,))
    self.p.start()

  def destroy(self):
    self.sub.stdin.close()
    self.p.terminate()
    self.p.join()
    # Kill the process group headed by the subprocess
    os.killpg(self.sub.pid, 15)

  def run_flux_cmd(self, command=''):
    global flux_exe
    print("{} {}".format(flux_exe, command), file=self.sub.stdin)

  def run_cmd(self, command=''):
    global flux_exe
    print(command, file=self.sub.stdin)

  def __del__(self):
    self.destroy()

@contextlib.contextmanager
def run_beside_flux(size=1):
  f = SideFlux(size)
  # print json.dumps(dict(os.environ))
  try:
    yield f
  finally:
    f.destroy()


if __name__ == '__main__':
  with run_beside_flux(1) as fp:
    while True:
      pass
