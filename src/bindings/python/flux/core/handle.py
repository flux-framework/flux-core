###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import six

from flux.wrapper import Wrapper
from flux.rpc import RPC
from flux.mrpc import MRPC
from flux.message import Message
from flux.core.inner import raw
from _flux._core import ffi, lib


class Flux(Wrapper):
    """
  The general Flux handle class, create one of these to connect to the
  nearest enclosing flux instance
  >>> flux.Flux() #doctest: +ELLIPSIS
  <flux.core.Flux object at 0x...>
  """

    def __init__(self, url=ffi.NULL, flags=0, handle=None):
        super(Flux, self).__init__(
            ffi,
            lib,
            handle=handle,
            match=ffi.typeof(lib.flux_open).result,
            filter_match=False,
            prefixes=["flux_", "FLUX_"],
            destructor=raw.flux_close,
        )

        if handle is None:
            self.handle = raw.flux_open(url, flags)

        self.aux_txn = None

    # pylint: disable=no-self-use
    def close(self):
        """
        The underlying flux handle is automatically closed when a Flux instance is
        deconstructed.  Prevent users from manually closing, the handle, leading
        to a double free.
        """
        raise RuntimeError(
            "Unnecessary manual invocation of a Flux handle's close method via "
            "the python bindings.  Handles are automatically closed when the "
            "Python object is deconstructed."
        )

    def log(self, level, fstring):
        """
        Log to the flux logging facility

        :param level: A syslog log-level, check the syslog module for possible
               values
        :param fstring: A string to log, C-style formatting is *not* supported
        """
        # Short-circuited because variadics can't be wrapped cleanly
        if isinstance(fstring, six.text_type):
            fstring = fstring.encode("utf-8")
        lib.flux_log(self.handle, level, fstring)

    def send(self, message, flags=0):
        """Send a pre-constructed flux message"""
        if isinstance(message, Message):
            message = message.handle
        return self.flux_send(message, flags)

    def recv(
        self,
        type_mask=raw.FLUX_MSGTYPE_ANY,
        match_tag=raw.FLUX_MATCHTAG_NONE,
        topic_glob=None,
        flags=0,
    ):
        """
        Receive a message, returns a flux.Message containing the result or None
        """
        match = ffi.new(
            "struct flux_match *",
            {
                "typemask": type_mask,
                "matchtag": match_tag,
                "topic_glob": topic_glob if topic_glob is not None else ffi.NULL,
            },
        )
        handle = self.flux_recv(match[0], flags)
        if handle is not None:
            return Message(handle=handle)
        return None

    def rpc(self, topic, payload=None, nodeid=raw.FLUX_NODEID_ANY, flags=0):
        """ Create a new RPC object """
        return RPC(self, topic, payload, nodeid, flags)

    def mrpc_create(self, topic, payload=None, rankset="any", flags=0):
        """
        Create a new MRPC object. Messages are sent on MRPC creation. Responses
        are accessible by iterating on the returned MRPC object. For example:
        '''
        for (response_nodeid, response_payload) in h.mrpc_create(...):
            pass
        '''
        """
        return MRPC(self, topic, payload, rankset, flags)

    def event_create(self, topic, payload=None):
        """ Create a new event message.

        :param topic: A string, the event's topic
        :param payload: If a string, the payload is used unmodified, if it is
            another type json.dumps() is used to stringify it
        """
        # pylint: disable=no-self-use
        return Message.from_event_encode(topic, payload)

    def event_send(self, topic, payload=None):
        """ Create and send a new event in one step """
        return self.send(self.event_create(topic, payload))

    def event_recv(self, topic=None):
        return self.recv(type_mask=raw.FLUX_MSGTYPE_EVENT, topic_glob=topic)

    def msg_watcher_create(
        self,
        callback,
        type_mask=raw.FLUX_MSGTYPE_ANY,
        topic_glob="*",
        args=None,
        match_tag=raw.FLUX_MATCHTAG_NONE,
    ):
        from flux.message import MessageWatcher

        return MessageWatcher(self, type_mask, callback, topic_glob, match_tag, args)

    def timer_watcher_create(self, after, callback, repeat=0.0, args=None):
        from flux.core.watchers import TimerWatcher

        return TimerWatcher(self, after, callback, repeat=repeat, args=args)

    def barrier(self, name, nprocs):
        self.flux_barrier(name, nprocs)

    def get_rank(self):
        rank = ffi.new("uint32_t [1]")
        self.flux_get_rank(rank)
        return rank[0]
