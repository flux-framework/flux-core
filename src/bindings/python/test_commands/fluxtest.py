import os
import sys
import re
import subprocess

# TODO make this install portable
flux_exe = os.path.join(os.path.dirname(os.path.abspath(__file__)),os.path.sep.join(['..','..','..','cmd','flux']))

def run_under_flux(size=1):
  if 'IN_SUBFLUX' in os.environ:
    return True
  os.environ['IN_SUBFLUX'] = '1'
  command = [str(flux_exe), 'start',
      '-o','-q',
      '-s',str(size),
      '--',
      sys.executable] + (sys.argv)
  print ' '.join(command)
  os.execv(command[0], command)
  return False


