from __future__ import print_function
import sys, os, re, flux

def list_instances(sid=None):
    tmpdir = '/tmp'
    tmpdir = os.environ.get('TMPDIR', tmpdir)

    fdir = re.compile('flux-(?P<id>[^-]+)-')

    for dirname, dirs, files in os.walk(tmpdir, topdown=True):
        for m in [fdir.match(d) for d in dirs]:
            if not m: continue
            if sid is not None and not re.search('flux-' + sid + '-',
                                                 m.string):
                continue
            job = os.path.join (os.path.join(tmpdir, m.string), '0')
            uri = 'local://' + job
            try:
                with open(os.path.join(job, 'broker.pid')) as f:
                    pid = int(f.readline())
                os.kill(pid, 0)
                f = flux.open(uri)
                yield (m.group('id'), uri)
            except:
                pass
        break
