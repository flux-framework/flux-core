import syslog
import flux


def echo_cb(h, typemask, message, arg):
    h.log(syslog.LOG_INFO, "in cb, args:{}".format((typemask, message, arg)))
    h.respond(message, message.payload_str)
    return 0


def mod_main(h, *args):
    with h.msg_watcher_create(echo_cb, topic_glob="echo.*") as mw:
        # N.B.: *only* do not use the wrapped h.reactor_run() here, since
        #  this will cause an assertion failure in libev. This is because
        #  only one ev_signal watcher can be installed per-process, and
        #  one is already being used in the broker.
        #
        if h.flux_reactor_run(h.get_reactor(), 0) < 0:
            h.fatal_error("reactor start failed!")
        h.log(syslog.LOG_INFO, "echo unloading")
