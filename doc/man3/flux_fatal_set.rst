=================
flux_fatal_set(3)
=================


SYNOPSIS
========

#include <flux/core.h>

typedef void (\*flux_fatal_f)(const char \*msg, void \*arg);

void flux_fatal_set (flux_t \*h, flux_fatal_f fun, void \*arg);

void flux_fatal_error (flux_t \*h, const char \*fun, const char \*msg);

FLUX_FATAL (flux_t \*h);


DESCRIPTION
===========

``flux_fatal_set()`` configures an optional fatal error function *fun* to
be called internally by ``libflux_core`` if an error occurs that is fatal
to the handle *h*. A fatal error is any error that renders the handle
unusable. The function may log *msg*, terminate the program,
or take other action appropriate to the application.

If a fatal error function is not registered, or if the fatal error
function returns, error handling proceeds as normal.

The fatal error function will only be called once, for the first
fatal error encountered.

*arg* is an optional argument passed through to the fatal error function.

``FLUX_FATAL()`` is a macro that calls

::
   flux_fatal_error (h, __FUNCTION__, strerror (errno))

which translates to a fatal error function called with *msg* set to
"function name: error string".


RESOURCES
=========

Github: http://github.com/flux-framework


SEE ALSO
========

flux_open(3)
