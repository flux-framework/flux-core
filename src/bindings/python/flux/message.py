import json

import six

from flux.wrapper import Wrapper, WrapperPimpl
from flux.core.inner import ffi, lib, raw
from flux.core.watchers import Watcher
import flux.constants

__all__ = ['Message',
           'MessageWatcher',
           'msg_typestr']


def msg_typestr(msg_type):
    return ffi.string(raw.flux_msg_typestr(msg_type))


class Message(WrapperPimpl):
    """ Flux message wrapper class. """

    class InnerWrapper(Wrapper):

        def __init__(self,
                     type_id=flux.constants.FLUX_MSGTYPE_REQUEST,
                     handle=None,
                     destruct=False,):
            super(self.__class__, self).__init__(
                ffi, lib,
                handle=handle,
                match=ffi.typeof(lib.flux_msg_create).result,
                prefixes=[
                    'flux_msg_',
                    'FLUX_MSG',
                ],
                destructor=raw.flux_msg_destroy if destruct else None,)
            if handle is None:
                self.handle = raw.flux_msg_create(type_id)

        def __del__(self):
            if ((not self.external or self.destruct) and
                    self.handle is not None and self.handle != ffi.NULL):
                raw.flux_msg_destroy(self.handle)

    def __init__(self,
                 type_id=flux.constants.FLUX_MSGTYPE_REQUEST,
                 handle=None,
                 destruct=False,):
        super(Message, self).__init__()
        self.pimpl = self.InnerWrapper(type_id, handle, destruct)

    @property
    def handle(self):
        return self.pimpl.handle

    @classmethod
    def from_event_encode(cls, topic, payload=None):
        if payload is None:
            payload = ffi.NULL
        elif not isinstance(payload, six.binary_type):
            # Convert dict or list into json string
            payload = json.dumps(payload)
        handle = raw.flux_event_encode(topic, payload)
        return cls(handle=handle)

    @property
    def topic(self):
        topic_string = ffi.new('char *[1]')
        self.pimpl.get_topic(topic_string)
        return ffi.string(topic_string[0])

    @topic.setter
    def topic(self, value):
        self.pimpl.set_topic(value)

    @property
    def payload_str(self):
        string = ffi.new('char *[1]')
        if self.pimpl.has_payload():
            self.pimpl.get_string(ffi.cast('char**', string))
            return ffi.string(string[0])
        else:
            return None

    @payload_str.setter
    def payload_str(self, value):
        self.pimpl.set_json(value)

    @property
    def payload(self):
        return json.loads(self.payload_str)

    @payload.setter
    def payload(self, value):
        self.payload_str = json.dumps(value)

    @property
    def type(self):
        message_type = ffi.new('int [1]')
        self.pimpl.get_type(message_type)
        return message_type[0]

    @type.setter
    def type(self, value):
        self.pimpl.set_type(value)

    @property
    def type_str(self):
        return msg_typestr(self.type)

# Residing here to avoid cyclic references


@ffi.callback('flux_msg_handler_f')
def message_handler_wrapper(unused1, unused2, msg_handle, opaque_handle):
    del unused1, unused2  # unused arguments
    watcher = ffi.from_handle(opaque_handle)
    watcher.callback(watcher.flux_handle, watcher,
                     Message(handle=msg_handle,
                             destruct=False), watcher.args)


class MessageWatcher(Watcher):

    def __init__(self, flux_handle, type_mask, callback,
                 topic_glob='*',
                 match_tag=flux.constants.FLUX_MATCHTAG_NONE,
                 args=None):
        self.flux_handle = flux_handle
        self.callback = callback
        self.args = args
        self.wargs = ffi.new_handle(self)
        c_topic_glob = ffi.new('char[]', topic_glob)
        match = ffi.new('struct flux_match *', {
            'typemask': type_mask,
            'matchtag': match_tag,
            'topic_glob': c_topic_glob,
        })
        super(MessageWatcher, self).__init__(
            raw.flux_msg_handler_create(
                self.flux_handle.handle,
                match[0],
                message_handler_wrapper,
                self.wargs))

    def start(self):
        raw.flux_msg_handler_start(self.handle)

    def stop(self):
        raw.flux_msg_handler_stop(self.handle)

    def destroy(self):
        if self.handle is not None:
            raw.flux_handler_destroy(self.handle)
            self.handle = None
