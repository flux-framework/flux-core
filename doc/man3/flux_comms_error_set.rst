=======================
flux_comms_error_set(3)
=======================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/core.h>

   typedef int (*flux_comms_error_f)(flux_t *h, void *arg);

   void flux_comms_error_set (flux_t *h,
                              flux_comms_error_f fun,
                              void *arg);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_comms_error_set` configures an optional callback :var:`fun` to
be called internally by ``libflux_core`` if an error occurs when sending
or receiving messages on the handle :var:`h`.

:var:`arg` is an optional argument passed through to the callback function.

The callback may assume that :var:`errno` is valid.  A typical callback in an
application might log the error and then exit.

If a comms error function is not registered, or if the function returns -1,
error handling proceeds as normal.  Be aware that further access attempts
to :var:`h` are likely to fail and the callback may be invoked again.

In advanced use cases, the callback may resolve the error and return 0,
in which case the errant low level message send or receive call is retried.
This mode should be considered experimental at this time.


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man3:`flux_open`
