==================================
flux_msg_handler_allow_rolemask(3)
==================================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/core.h>

   void flux_msg_handler_allow_rolemask (flux_msg_handler_t *mh,
                                         uint32_t rolemask);

   void flux_msg_handler_deny_rolemask (flux_msg_handler_t *mh,
                                        uint32_t rolemask);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

Flux's role based access control mechanism is described in RFC 12.
The message handler rolemask determines which messages are delivered
to the handler.  Requests that would otherwise match but cannot be
delivered due to a missing role are automatically sent a "permission
denied" response.  Other message types that don't match are silently
discarded.  The instance owner is implicitly authorized for every
service, so it is unnecessary to add FLUX_ROLE_OWNER to the role mask.

The currently supported roles are:

FLUX_ROLE_OWNER
   Requests from instance owners are matched.

FLUX_ROLE_USER
   Requests from users / guests can be matched.

FLUX_ROLE_LOCAL
   Requests from the same broker as the receiver are matched.

By default, message handlers have a rolemask of FLUX_ROLE_OWNER.

:func:`flux_msg_handler_allow_rolemask` and
:func:`flux_msg_handler_deny_rolemask` can be used to alter the
rolemask for each message handler.


RESOURCES
=========

.. include:: common/resources.rst


FLUX RFC
========

:doc:`rfc:spec_12`


SEE ALSO
========

:man3:`flux_msg_handler_addvec`, :man3:`flux_msg_handler_create`, :man3:`flux_msg_cmp`
