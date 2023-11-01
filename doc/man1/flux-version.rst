.. flux-help-description : Display flux version information

===============
flux-version(1)
===============


SYNOPSIS
========

**flux** **version**


DESCRIPTION
===========

:program:`flux version` prints version information for flux components.
At a minimum, the version of flux commands and the currently linked
libflux-core.so library is displayed. If running within an instance,
the version of the flux-broker found and FLUX_URI are also included.
Finally, if flux is compiled against flux-security, then the version
of the currently linked libflux-security is included.


RESOURCES
=========

Flux: http://flux-framework.org
