from flux.wrapper import Wrapper, WrapperPimpl
from flux.core.inner import ffi, lib, raw
import flux
import json


def msg_typestr(t):
    return ffi.string(raw.flux_msg_typestr(t))


class Message(WrapperPimpl):
    """ Flux message wrapper class. """

    class InnerWrapper(Wrapper):

        def __init__(self,
                     type_id=flux.FLUX_MSGTYPE_REQUEST,
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
                 type_id=flux.FLUX_MSGTYPE_REQUEST,
                 handle=None,
                 destruct=False,):
        self.pimpl = self.InnerWrapper(type_id, handle, destruct)

    @property
    def handle(self):
        return self.pimpl.handle

    @classmethod
    def from_event_encode(cls, topic, payload=None):
        if payload is None:
            payload = ffi.NULL
        elif not isinstance(payload, basestring):
            # Convert dict or list into json string
            payload = json.dumps(payload)
        handle = raw.flux_event_encode(topic, payload)
        return cls(handle=handle)

    @property
    def topic(self):
        s = ffi.new('char *[1]')
        self.pimpl.get_topic(s)
        return ffi.string(s[0])

    @topic.setter
    def topic(self, value):
        self.pimpl.set_topic(value)

    @property
    def payload_str(self):
        s = ffi.new('char *[1]')
        if self.pimpl.has_payload():
            self.pimpl.get_json(ffi.cast('char**', s))
            return ffi.string(s[0])
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
        s = ffi.new('int [1]')
        self.pimpl.get_type(s)
        return s[0]

    @type.setter
    def type(self, value):
        self.pimpl.set_type(value)

    @property
    def type_str(self):
        return msg_typestr(self.type)
