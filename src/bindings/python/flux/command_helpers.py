from __future__ import print_function
import sys, os, re, flux

def list_instances(list_all, top_only, sid=None):
    tmpdir = '/tmp'
    tmpdir = os.environ.get('TMPDIR', tmpdir)
    if not list_all:
        tmpdir = os.environ.get('FLUX_TMPDIR', tmpdir)

    fdir = re.compile('flux-(?P<id>[^-]+)-0')

    # Sometimes flux tmpdirs end up in sub-directories of previous tmpdirs
    for dirname, dirs, files in os.walk(tmpdir, topdown=True):
        for m in [fdir.match(d) for d in dirs]:
            if not m: continue
            if sid is not None and not re.search('flux-' + sid + '-0',
                                                 m.string):
                continue
            job = os.path.join(tmpdir, m.string)
            uri = 'local://' + job
            try:
                with open(os.path.join(job, 'broker.pid')) as f:
                    pid = int(f.readline())
                os.kill(pid, 0)
                f = flux.open(uri)
                yield (m.group('id'), uri)
            except:
                pass
        if top_only:
            break
