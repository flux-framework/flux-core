import syslog
import flux


def echo_cb(h, typemask, message, arg):
    h.log(syslog.LOG_INFO, "in cb, args:{}".format((typemask, message, arg)))
    h.respond(message, 0, message.payload_str)
    return 0


def mod_main(h, *args):
    with h.msg_watcher_create(echo_cb, topic_glob="echo.*") as mw:
        if h.reactor_run(h.get_reactor(), 0) < 0:
            h.fatal_error("reactor start failed!")
        h.log(syslog.LOG_INFO, "echo unloading")
