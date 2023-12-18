====================
flux_msg_has_flag(3)
====================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

  #include <flux/core.h>

  bool flux_msg_has_flag (const flux_msg_t *msg, int flag);
  int flux_msg_set_flag (flux_msg_t *msg, int flag);
  int flux_msg_clear_flag (flux_msg_t *msg, int flag);

  int flux_msg_set_private (flux_msg_t *msg);
  bool flux_msg_is_private (const flux_msg_t *msg);

  int flux_msg_set_streaming (flux_msg_t *msg);
  bool flux_msg_is_streaming (const flux_msg_t *msg);

  int flux_msg_set_noresponse (flux_msg_t *msg);
  bool flux_msg_is_noresponse (const flux_msg_t *msg);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

These functions manipulate the Flux `MESSAGE FLAGS`_.

:func:`flux_msg_has_flag` returns true if any flags in the :var:`flag` bitmask
are set in :var:`msg`.

:func:`flux_msg_set_flag` sets all flags in the :var:`flag` bitmask in
:var:`msg`.

:func:`flux_msg_clear_flag` clears all flags in the :var:`flag` bitmask in
:var:`msg`.

:func:`flux_msg_is_private` returns true if FLUX_MSGFLAG_PRIVATE is set
in :var:`msg`.

:func:`flux_msg_set_private` sets FLUX_MSGFLAG_PRIVATE in :var:`msg`.

:func:`flux_msg_is_streaming` returns true if FLUX_MSGFLAG_STREAMING is set
in :var:`msg`.

:func:`flux_msg_set_streaming` sets FLUX_MSGFLAG_STREAMING in :var:`msg`.

:func:`flux_msg_is_noresponse` returns true if FLUX_MSGFLAG_NORESPONSE is set
in :var:`msg`.

:func:`flux_msg_set_noresponse` sets FLUX_MSGFLAG_NORESPONSE in :var:`msg`.

MESSAGE FLAGS
=============

The following message flags are defined by :doc:`RFC 3 <rfc:spec_3>`:

FLUX_MSGFLAG_TOPIC
  The message has a topic string.  This flag is updated by
  :func:`flux_msg_set_topic`.

FLUX_MSGFLAG_PAYLOAD
  The message has a payload.  This flag is updated by
  :func:`flux_msg_set_payload`, :func:`flux_msg_pack`, etc.

FLUX_MSGFLAG_NORESPONSE
  The request message should not be sent a response.  This flag is set
  by :func:`flux_rpc` when the FLUX_RPC_NORESPONSE flag is set.

FLUX_MSGFLAG_ROUTE
  The request or response message has a route stack, although it may be empty.
  This flag is updated by :func:`flux_msg_route_enable` and
  :func:`flux_msg_route_disable`.

FLUX_MSGFLAG_UPSTREAM
  Force the broker to route a request upstream (towards the root on the tree
  based overlay network) relative to the sending rank.  In other words,
  prevent the request from being handled locally.  The message :var:`nodeid`
  is interpreted as the *sending* rank when this flag is set.

FLUX_MSGFLAG_PRIVATE
  The event message should only be forwarded to connections authenticated
  as the instance owner or the message :var:`userid`.

FLUX_MSGFLAG_STREAMING
  The request or response message is part of a streaming RPC, as defined
  by  :doc:`RFC 6 <rfc:spec_6>`.  This flag is set by :func:`flux_rpc` when
  the FLUX_RPC_STREAMING flag is set.

FLUX_MSGFLAG_USER1
  This flag is opaque to Flux's message handling semantics and may be assigned
  application-specific meaning in the same way as the message payload.

RETURN VALUE
============

:func:`flux_msg_has_flag`, :func:`flux_msg_is_private`,
:func:`flux_msg_is_streaming`, and :func:`flux_msg_is_noresponse` return
true if the specified flag is set, or false if if the flag is not set or
the arguments are invalid.

The remaining functions return 0 on success. On error, -1 is returned,
and :var:`errno` is set appropriately.

ERRORS
======

EINVAL
   Some arguments were invalid.

ENOMEM
   Out of memory.

RESOURCES
=========

.. include:: common/resources.rst

FLUX RFC
========

:doc:`rfc:spec_3`

:doc:`rfc:spec_6`

SEE ALSO
========

:man3:`flux_rpc`
