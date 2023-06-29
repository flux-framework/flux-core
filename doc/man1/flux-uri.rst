===========
flux-uri(1)
===========


SYNOPSIS
========

**flux** *uri* [OPTIONS] *TARGET*

DESCRIPTION
===========

Connections to Flux are established via a Uniform Resource Identifier
(URI) which is passed to the :man3:`flux_open` API call. These *native*
URIs indicate the "connector" which will be used to establish the
connection, and are typically either *local*, with a  ``local`` URI
scheme, or *remote*, with a ``ssh`` URI scheme. These URIs are considered
fully-resolved, native Flux URIs.

Processes running within a Flux instance will have the ``FLUX_URI``
environment variable set to a native URI which :man3:`flux_open` will
use by default, with fallback to a compiled-in native URI for the system
instance of Flux. Therefore, there is usually no need to specify a URI when
connecting to the enclosing instance. However, connecting to a *different*
Flux instance will require discovery of the fully-resolved URI for that
instance.

**flux uri** attempts to resolve its *TARGET* argument to a native local
or remote URI. The *TARGET* is itself a URI which specifies the method
to use in URI resolution via the scheme part, with the path and query
parts passed to a plugin which implements the resolution method.

As a convenience, if *TARGET* is specified with no scheme, then the scheme
is assumed to be ``jobid``.  This allows ``flux uri`` to be used to look
up the URI for a Flux instance running as a job in the current enclosing
instance with:

::

   $ flux uri JOBID

Depending on the *TARGET* URI scheme and corresponding plugin, specific
query arguments in *TARGET* may be supported. However, as a convenience,
all *TARGET* URIs support the special query arguments ``local`` or
``remote`` to force the resulting URI into a local (``local://``) or remote
(``ssh://``) form. For example:

::

   $ flux uri JOBID?local

would return the ``local://`` URI for *JOBID* (if the URI can be resolved).

A special environment variable FLUX_URI_RESOLVE_LOCAL will force
``flux uri`` to always resolve URIs to local form.  This is often useful if
the local connector is known to be on the local system (i.e. within a test
Flux instance), and ssh to localhost does not work.

A list of supported URI schemes will be listed at the bottom of
``flux uri --help`` message. For a description of the URI resolver schemes
included with Flux, see the URI SCHEMES and EXAMPLES sections below.

OPTIONS
=======

**--remote**
   Return the *remote* (``ssh://``)  equivalent of the resolved URI.

**--local**
   Return the *local* (``local://``) equivalent of the resolved URI.
   Warning: the resulting URI may be invalid for the current system
   if the network host specified by an ``ssh`` URI is not the current
   host.

URI SCHEMES
===========

The following URI schemes are included by default:

jobid:ID[/ID...]
   This scheme attempts to get the URI for a Flux instance running as a
   job in the current enclosing instance. This is the assumed scheme if no
   ``scheme:`` is provided in *TARGET* passed to ``flux uri``, so the
   ``jobid:`` prefix is optional. A hierarchy of Flux jobids is supported,
   so ``f1234/f3456`` will resolve the URI for job ``f3456`` running in
   job ``f1234`` in the current instance. This scheme will raise an error
   if the target job is not running.

pid:PID
  This scheme attempts to read the ``FLUX_URI`` value from the process id
  *PID* using ``/proc/PID/environ``. If *PID* refers to a ``flux-broker``,
  then the scheme reads ``FLUX_URI`` from the broker's initial program or
  another child process since ``FLUX_URI`` in the broker's environment
  would refer to *its* parent (or may not be set at all in the case of a
  test instance started with ``flux start --test-size=N``).

slurm:JOBID
  This scheme makes a best-effort to resolve the URI of a Flux instance
  launched under Slurm. It invokes ``srun`` to run ``scontrol listpids``
  on the first node of the job, and then uses the ``pid`` resolver until
  it finds a valid ``FLUX_URI``.


EXAMPLES
========

To get the URI of a job in the current instance in its ``local://`` form:

::

   $ flux uri --local ƒN8Pz2xVu
   local:///tmp/flux-zbVtVg/jobtmp-0-ƒN8Pz2xVu/flux-59uf5w/local-0

or

::

   $ flux uri ƒN8Pz2xVu?local
   local:///tmp/flux-zbVtVg/jobtmp-0-ƒN8Pz2xVu/flux-59uf5w/local-0


Get the URI of a nested job:

::

   $ flux uri ƒqxxTiZBM/ƒr2XFWP?local
   local:///tmp/flux-zbVtVg/jobtmp-0-ƒqxxTiZBM/flux-EPgSwk/local-0

.. note::
   With  the ``jobid`` resolver, ``?local`` only needs to be placed on
   the last component of the jobid "path" or hierarchy. This will resolve
   each URI in turn as a local URI.

Get the URI of a local flux-broker

::

   $ flux uri pid:$(pidof -s flux-broker)
   local:///tmp/flux-sLuBkZ/local-0

Get the URI for a Flux instance running as a Slurm job:

::

   $ flux uri slurm:7843494
   ssh://cluster42/var/tmp/user/flux-MpnytT/local-0


RESOURCES
=========

Flux: http://flux-framework.org
