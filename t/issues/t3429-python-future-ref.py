#!/usr/bin/env python3
#
#  Test for issue #3429 - circular future reference occurs when
#   an exception occurs in a Future continuation_callback. Then
#   the future is not garbage collected and Python hangs in the
#   Flux reactor
#
import flux


def ping_cb(rpc):
    print(rpc.get_str())


h = flux.Flux()
print("asynchronous: ping kvs.foo")
h.rpc("kvs.foo", {}).then(ping_cb)
try:
    rc = h.reactor_run()
except OSError as exc:
    print(f"Got exception: {exc}")

print("Done")
