from flux.wrapper import Wrapper, WrapperPimpl
from flux.core.inner import ffi, lib
import flux
import json

class Message(WrapperPimpl):
  class InnerWrapper(Wrapper):
    def __init__(self, type_id=flux.FLUX_MSGTYPE_REQUEST, handle=None, destruct=False):
      self.destruct = destruct
      if handle is None:
        self.external = False
        handle = lib.flux_msg_create(type_id)
      else:
        self.external = True
      super(self.__class__, self).__init__(ffi, lib, handle=handle,
                                       match=ffi.typeof(lib.flux_msg_create).result,
                                       prefixes=[
                                         'flux_msg_',
                                         'FLUX_MSG',
                                         ],
                                       )
    def __del__(self):
      if ((not self.external or self.destruct) 
          and self.handle is not None and self.handle != ffi.NULL):
        lib.flux_msg_destroy(self.handle)

  def __init__(self, type_id=flux.FLUX_MSGTYPE_REQUEST, handle=None, destruct=False):
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
    handle = lib.flux_event_encode(topic, payload)
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
      self.pimpl.get_payload_json(ffi.cast('char**',s))
      return ffi.string(s[0])
    else:
      return None

  @payload_str.setter
  def payload_str(self, value):
    self.pimpl.set_payload_json(value)

  @property
  def payload(self):
    return json.loads(self.payload_str)

  @payload.setter
  def payload(self, value):
    self.payload_str = json.dumps(value)
