from __future__ import print_function
import os
import re
import flux


def list_instances(sid=None):
    tmpdir = '/tmp'
    tmpdir = os.environ.get('TMPDIR', tmpdir)

    fdir = re.compile('flux-(?P<id>[^-]+)-')

    for dirname, dirs, files in os.walk(tmpdir, topdown=True):
        for match in [fdir.match(d) for d in dirs]:
            if not match:
                continue
            if sid is not None and not re.search('flux-' + sid + '-',
                                                 match.string):
                continue
            job = os.path.join(os.path.join(tmpdir, match.string), '0')
            uri = 'local://' + job
            try:
                with open(os.path.join(job, 'broker.pid')) as pidfile:
                    pid = int(pidfile.readline())
                os.kill(pid, 0)
                flux.Flux(uri)
                yield (match.group('id'), uri)
            except:
                pass
        break
