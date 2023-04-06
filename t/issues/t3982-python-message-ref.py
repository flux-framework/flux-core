#!/usr/bin/env python3
#
#  Test for issue #3982 - Ensure python handles message refcount properly
#
import sys
import flux
from flux.constants import FLUX_MSGTYPE_REQUEST

_SAVED_MESSAGES = []


def create_cb(fh, t, msg, timer):
    _SAVED_MESSAGES.append(msg)
    print("server: got request, starting timer", file=sys.stderr)
    timer.start()


def timer_cb(fh, *args, **kwargs):
    try:
        msg = _SAVED_MESSAGES.pop()
    except IndexError:
        pass
    else:
        fh.respond(msg, {"success": True})
        print("server: responded to message", file=sys.stderr)


def rpc_cb(future):
    if future.get()["success"]:
        print("client: Success", file=sys.stderr)
        future.get_flux().reactor_stop()
    else:
        future.get_flux().reactor_stop_error()


fh = flux.Flux()
fh.service_register("t3982").get()

timer = fh.timer_watcher_create(0.01, timer_cb)

w = fh.msg_watcher_create(create_cb, FLUX_MSGTYPE_REQUEST, "t3982.test", timer)
w.start()

fh.rpc("t3982.test", {}).then(rpc_cb)
print("client: Sent request", file=sys.stderr)

fh.reactor_run()
