###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import errno
import json

import flux.constants
from flux.core.inner import ffi, lib, raw
from flux.core.watchers import Watcher
from flux.util import encode_payload, encode_topic
from flux.wrapper import Wrapper, WrapperPimpl

__all__ = ["Message", "MessageWatcher", "msg_typestr"]


def msg_typestr(msg_type):
    # the returned string is guaranteed to be ascii
    return raw.flux_msg_typestr(msg_type).decode("ascii")


class Message(WrapperPimpl):
    """Flux message wrapper class."""

    class InnerWrapper(Wrapper):
        def __init__(
            self,
            type_id=flux.constants.FLUX_MSGTYPE_REQUEST,
            handle=None,
        ):
            super(Message.InnerWrapper, self).__init__(
                ffi,
                lib,
                handle=handle,
                match=ffi.typeof(lib.flux_msg_create).result,
                prefixes=["flux_msg_", "FLUX_MSG"],
                destructor=raw.flux_msg_destroy,
            )
            if handle is None:
                self.handle = raw.flux_msg_create(type_id)
            else:
                #  Always take a reference on externally supplied flux_msg_t
                #   so that it can't be destroyed while the Python
                #   Message object is still active.
                raw.flux_msg_incref(self.handle)

        def __del__(self):
            if self.handle is not None and self.handle != ffi.NULL:
                raw.flux_msg_decref(self.handle)

    def __init__(self, type_id=flux.constants.FLUX_MSGTYPE_REQUEST, handle=None):
        super(Message, self).__init__()
        self.pimpl = self.InnerWrapper(type_id, handle)

    @classmethod
    def from_event_encode(cls, topic, payload=None):
        payload = encode_payload(payload)
        handle = raw.flux_event_encode(topic, payload)
        return cls(handle=handle)

    @property
    def topic(self):
        topic_string = ffi.new("char *[1]")
        self.pimpl.get_topic(topic_string)
        return ffi.string(topic_string[0]).decode("utf-8")

    @topic.setter
    def topic(self, value):
        topic = encode_topic(value)
        self.pimpl.set_topic(topic)

    @property
    def payload_str(self):
        string = ffi.new("char *[1]")
        if self.pimpl.has_payload():
            self.pimpl.get_string(ffi.cast("char**", string))
            return ffi.string(string[0]).decode("utf-8")
        return None

    @payload_str.setter
    def payload_str(self, value):
        self.pimpl.set_string(value)

    @property
    def payload(self):
        return json.loads(self.payload_str)

    @payload.setter
    def payload(self, value):
        self.payload_str = encode_payload(value)

    @property
    def type(self):
        message_type = ffi.new("int [1]")
        self.pimpl.get_type(message_type)
        return message_type[0]

    @type.setter
    def type(self, value):
        self.pimpl.set_type(value)

    @property
    def type_str(self):
        return msg_typestr(self.type)

    def decode(self):
        """Decode a message

        Attempt to decode a message and return the message type, topic,
        and payload string if successful.

        Returns:
            type: FLUX_MSGTYPE_REQUEST or FLUX_MSGTYPE_RESPONSE
            topic: topic string
            payload: payload string if one exists
        Raises:
            OSError: if message is an error response, raises OSError with
                exception.errno set
        """
        topic_string = ffi.new("char *[1]")
        string = ffi.new("char *[1]")
        if self.type == flux.constants.FLUX_MSGTYPE_REQUEST:
            raw.flux_request_decode(self.handle, topic_string, string)
        elif self.type == flux.constants.FLUX_MSGTYPE_RESPONSE:
            raw.flux_response_decode(self.handle, topic_string, string)
        elif self.type == flux.constants.FLUX_MSGTYPE_EVENT:
            raw.flux_event_decode(self.handle, topic_string, string)
        payload_str = None
        if string[0] != ffi.NULL:
            payload_str = ffi.string(string[0]).decode("utf-8")
        return (
            self.type,
            ffi.string(topic_string[0]).decode("utf-8"),
            payload_str,
        )


# Residing here to avoid cyclic references


@ffi.def_extern()
def message_handler_wrapper(unused1, unused2, msg_handle, opaque_handle):
    del unused1, unused2  # unused arguments
    watcher = ffi.from_handle(opaque_handle)
    try:
        watcher.callback(
            watcher.flux_handle,
            watcher,
            Message(handle=msg_handle),
            watcher.args,
        )
    # pylint: disable=broad-except
    except Exception as exc:
        type(watcher.flux_handle).set_exception(exc)
        watcher.flux_handle.reactor_stop_error()


class MessageWatcher(Watcher):
    def __init__(
        self,
        flux_handle,
        type_mask,
        callback,
        topic_glob="*",
        match_tag=flux.constants.FLUX_MATCHTAG_NONE,
        rolemask=None,
        args=None,
    ):
        self.callback = callback
        self.args = args
        self.wargs = ffi.new_handle(self)

        if topic_glob is None or topic_glob == ffi.NULL:
            topic_glob = ffi.NULL
        elif isinstance(topic_glob, str):
            topic_glob = topic_glob.encode("UTF-8")
        elif not isinstance(topic_glob, bytes):
            errmsg = "Topic must be a string, not {}".format(type(topic_glob))
            raise TypeError(errno.EINVAL, errmsg)
        c_topic_glob = ffi.new("char[]", topic_glob)

        match = ffi.new(
            "struct flux_match *",
            {"typemask": type_mask, "matchtag": match_tag, "topic_glob": c_topic_glob},
        )
        super(MessageWatcher, self).__init__(
            flux_handle,
            raw.flux_msg_handler_create(
                flux_handle.handle,
                match[0],
                lib.message_handler_wrapper,
                self.wargs,
            ),
        )
        if rolemask:
            flux_handle.msg_handler_allow_rolemask(self.handle, rolemask)

    def start(self):
        raw.flux_msg_handler_start(self.handle)

    def stop(self):
        raw.flux_msg_handler_stop(self.handle)

    def destroy(self):
        if self.handle is not None:
            raw.flux_msg_handler_destroy(self.handle)
            self.handle = None
