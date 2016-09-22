import flux
from flux.core.inner import raw, ffi
import flux.message
import abc

__all__ = ['MessageWatcher', 'TimerWatcher', 'FDWatcher']


class Watcher(object):
    __metaclass__ = abc.ABCMeta

    def __init__(self):
        pass

    def __enter__(self):
        """Allow this to be used as a context manager"""
        self.start()
        return self

    def __exit__(self, type_arg, value, tb):
        """Allow this to be used as a context manager"""
        self.stop()
        return False

    def __del__(self):
        if self.handle is not None:
            self.destroy()

    def start(self):
        raw.flux_watcher_start(self.handle)

    def stop(self):
        raw.flux_watcher_stop(self.handle)

    def destroy(self):
        if self.handle is not None:
            raw.flux_watcher_destroy(self.handle)
            self.handle = None


@ffi.callback('flux_msg_handler_f')
def message_handler_wrapper(handle_trash, m_watcher_t, msg_handle,
                            opaque_handle):
    watcher = ffi.from_handle(opaque_handle)
    watcher.cb(watcher.fh, watcher,
               flux.message.Message(handle=msg_handle,
                                    destruct=False), watcher.args)


class MessageWatcher(Watcher):

    def __init__(self, flux_handle, type_mask, callback,
                 topic_glob='*',
                 match_tag=flux.FLUX_MATCHTAG_NONE,
                 args=None):
        self.handle = None
        self.fh = flux_handle
        self.cb = callback
        self.args = args
        self.wargs = ffi.new_handle(self)
        c_topic_glob = ffi.new('char[]', topic_glob)
        match = ffi.new('struct flux_match *', {
            'typemask': type_mask,
            'matchtag': match_tag,
            'topic_glob': c_topic_glob,
        })
        self.handle = raw.flux_msg_handler_create(self.fh.handle, match[0],
                                                  message_handler_wrapper,
                                                  self.wargs)

    def start(self):
        raw.flux_msg_handler_start(self.handle)

    def stop(self):
        raw.flux_msg_handler_stop(self.handle)

    def destroy(self):
        if self.handle is not None:
            raw.flux_handler_destroy(self.handle)
            self.handle = None


@ffi.callback('flux_watcher_f')
def timeout_handler_wrapper(handle_trash, watcher_s, revents,
                            opaque_handle):
    watcher = ffi.from_handle(opaque_handle)
    watcher.cb(watcher.fh, watcher, revents, watcher.args)


class TimerWatcher(Watcher):

    def __init__(self, flux_handle, after, callback, repeat=0, args=None, ):
        self.fh = flux_handle
        self.after = after
        self.repeat = repeat
        self.cb = callback
        self.args = args
        self.handle = None
        self.wargs = ffi.new_handle(self)
        self.handle = raw.flux_timer_watcher_create(
            raw.flux_get_reactor(flux_handle),
            float(after), float(repeat), timeout_handler_wrapper, self.wargs)


@ffi.callback('flux_watcher_f')
def fd_handler_wrapper(handle_trash, fd_watcher_s, revents,
                       opaque_handle):
    watcher = ffi.from_handle(opaque_handle)
    fd_int = raw.fd_watcher_get_fd(watcher.handle)
    watcher.cb(watcher.fh, watcher, fd_int, revents, watcher.args)


class FDWatcher(Watcher):

    def __init__(self, flux_handle, fd_int, events, callback, args=None):
        self.fh = flux_handle
        self.fd_int = fd_int
        self.events = events
        self.cb = callback
        self.args = args
        self.handle = None
        self.wargs = ffi.new_handle(self)
        self.handle = raw.flux_fd_watcher_create(
            raw.flux_get_reactor(flux_handle),
            self.fd_int, self.events, fd_handler_wrapper, self.wargs)
