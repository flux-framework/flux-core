#!/bin/sh -e
# job-info: in-flight lookup requests are unanswered on module unload
#
# How this test works
#
# In an instance with no rc scripts loaded, a python script registers
# itself as the "kvs" service and never responds to kvs.lookup
# requests.  A job-info.lookup request is then guaranteed to be parked
# in job-info waiting on the KVS.  Once the fake kvs service sees the
# kvs.lookup request, the script unloads job-info and waits for a
# response to the job-info.lookup request.
#
# Before the fix, job-info destroys the pending lookup context on unload
# without responding, so the requester blocks forever and the test times
# out.

cat <<-EOF >t7884.py
import errno
import sys
import flux
from flux.constants import FLUX_MSGTYPE_REQUEST

TIMEOUT = 100.0

h = flux.Flux()

# hold on to kvs.lookup requests so they are never answered
saved = []
removing = False


def log(msg):
    print(f"t7884.py: {msg}", file=sys.stderr, flush=True)


def remove_cb(f):
    f.get()
    log("job-info unloaded")


def stats_cb(f):
    lookups = f.get()["lookups"]
    if lookups < 1:
        log(f"job-info reports {lookups} lookups in flight, expected 1")
        h.reactor_stop_error()
        return
    log(f"job-info reports {lookups} lookup(s) in flight, removing job-info")
    h.rpc("module.remove", {"name": "job-info"}).then(remove_cb)


# N.B. job-info may send more than one kvs.lookup per job-info.lookup
# (e.g. eventlog for the owner check plus the requested key), so only
# act on the first one.
def kvs_lookup_cb(h, t, msg, arg):
    global removing
    saved.append(msg)
    if removing:
        return
    removing = True
    log("fake kvs got kvs.lookup, checking job-info stats")
    h.rpc("job-info.stats-get").then(stats_cb)


def lookup_cb(f):
    try:
        f.get()
        log("job-info.lookup unexpectedly succeeded")
        h.reactor_stop_error()
    except OSError as e:
        if e.errno == errno.ENOSYS:
            log(f"job-info.lookup got expected error: {e.strerror}")
            h.reactor_stop()
        else:
            log(f"job-info.lookup got unexpected error: {e.strerror}")
            h.reactor_stop_error()


def timeout_cb(h, w, revents, arg):
    log(f"no response to job-info.lookup after {TIMEOUT}s")
    h.reactor_stop_error()


h.service_register("kvs").get()
log("registered fake kvs service")

w = h.msg_watcher_create(kvs_lookup_cb, FLUX_MSGTYPE_REQUEST, "kvs.lookup")
w.start()

timer = h.timer_watcher_create(TIMEOUT, timeout_cb)
timer.start()

h.rpc("job-info.lookup", {"id": 1234, "keys": ["jobspec"], "flags": 0}).then(
    lookup_cb
)

if h.reactor_run() < 0:
    sys.exit(1)
EOF

cat <<-EOF >t7884.sh
#!/bin/sh -e
flux module load job-info
flux python ./t7884.py
EOF

chmod +x t7884.sh

flux start -s 1 \
    --setattr=broker.rc1_path= \
    --setattr=broker.rc3_path= \
    ./t7884.sh
