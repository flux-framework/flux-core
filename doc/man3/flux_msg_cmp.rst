===============
flux_msg_cmp(3)
===============

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/core.h>

   struct flux_match {
       int typemask;
       uint32_t matchtag;
       char *topic_glob;
   };

   bool flux_msg_cmp (const flux_msg_t *msg, struct flux_match match);


DESCRIPTION
===========

:func:`flux_msg_cmp` compares :var:`msg` to :var:`match` criteria.

If :var:`match.typemask` is nonzero, the type of the message must match
one of the types in the mask.

If :var:`match.matchtag` is not FLUX_MATCHTAG_NONE, the message matchtag
must match :var:`match.matchtag`.

If :var:`match.topic_glob` is not NULL or an empty string, then the message
topic string must match :var:`match.topic_glob` according to the rules of
shell wildcards.


RETURN VALUE
============

:func:`flux_msg_cmp` returns true on a match, otherwise false.


RESOURCES
=========

Flux: http://flux-framework.org


SEE ALSO
========

:linux:man3:`fnmatch`
