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

All message handlers have a rolemask, which indicates what messages can be
matched.  The currently supported roles are:

FLUX_ROLE_OWNER
   Requests from instance owners are matched.

FLUX_ROLE_USER
   Requests from users / guests can be matched.

FLUX_ROLE_LOCAL
   Requests from the same broker as the receiver are matched.

By default, message handlers default its rolemask to FLUX_ROLE_OWNER.
:func:`flux_msg_handler_allow_rolemask` and
:func:`flux_msg_handler_deny_rolemask` can be used to alter the
rolemask for each message handler.


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man3:`flux_msg_handler_addvec`, :man3:`flux_msg_handler_create`, :man3:`flux_msg_cmp`
