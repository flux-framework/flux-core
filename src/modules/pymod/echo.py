import os
import syslog
import flux
from flux import core
from flux.mrpc import MRPC, Jobj
import sys

def pecho_impl(h, typemask, message, arg):
    print >>sys.stderr, "got here, I swear...", message.topic, message.payload_str
    h.log(syslog.LOG_INFO, "in impl, args:{}".format((typemask, message, arg)))
    rpc = MRPC.from_event(h, message.payload_str)
    rpc.outarg = rpc.inarg
    rpc.respond()
    return 0

def mod_main(h, **arg_dict):
    if h.event_subscribe("mrpc.mecho") < 0:
        h.fatal_error("event subscription failed")
    with h.msg_watcher_create(pecho_impl,
                                  type_mask=flux.FLUX_MSGTYPE_EVENT,
                                  pattern="mrpc.mecho") as mw:
        if h.reactor_start() < 0:
            h.fatal_error( "reactor start failed!")
    h.log(syslog.LOG_INFO, "pecho unloading")

