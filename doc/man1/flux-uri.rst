===========
flux-uri(1)
===========


SYNOPSIS
========

**flux** *uri* [OPTIONS] *TARGET*

DESCRIPTION
===========

.. program:: flux uri

Connections to Flux are established via a Uniform Resource Identifier
(URI) which is passed to the :man3:`flux_open` API call. These *native*
URIs indicate the "connector" which will be used to establish the
connection, and are typically either *local*, with a  ``local`` URI
scheme, or *remote*, with a ``ssh`` URI scheme. These URIs are considered
fully-resolved, native Flux URIs.

Processes running within a Flux instance will have the :envvar:`FLUX_URI`
environment variable set to a native URI which :man3:`flux_open` will
use by default, with fallback to a compiled-in native URI for the system
instance of Flux. Therefore, there is usually no need to specify a URI when
connecting to the enclosing instance. However, connecting to a *different*
Flux instance will require discovery of the fully-resolved URI for that
instance.

:program:`flux uri` attempts to resolve its *TARGET* argument to a native local
or remote URI. The *TARGET* is itself a URI which specifies the method
to use in URI resolution via the scheme part, with the path and query
parts passed to a plugin which implements the resolution method.

As a convenience, if *TARGET* is specified with no scheme, then the scheme
is assumed to be ``jobid``.  This allows :program:`flux uri` to be used to look
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

A special environment variable :envvar:`FLUX_URI_RESOLVE_LOCAL` will force
:program:`flux uri` to always resolve URIs to local form.  This is often useful
if the local connector is known to be on the local system (i.e. within a test
Flux instance), and ssh to localhost does not work.

A list of supported URI schemes will be listed at the bottom of
:option:`flux uri --help` message. For a description of the URI resolver
schemes included with Flux, see the URI SCHEMES and EXAMPLES sections below.

OPTIONS
=======

.. option:: --remote

   Return the *remote* (``ssh://``)  equivalent of the resolved URI.

.. option:: --local

   Return the *local* (``local://``) equivalent of the resolved URI.
   Warning: the resulting URI may be invalid for the current system
   if the network host specified by an :program:`ssh` URI is not the current
   host.

.. option:: --wait

   Wait for the URI to become available if the resolver scheme supports it.
   This is the same as specifying :command:`flux uri TARGET?wait`. Currently
   only supported by the ``jobid`` resolver. It will be ignored by other
   schemes.

URI SCHEMES
===========

The following URI schemes are included by default:

jobid:PATH
   This scheme attempts to get the URI for a Flux instance running as a
   job in the current enclosing instance. This is the assumed scheme if no
   ``scheme:`` is provided in *TARGET* passed to :program:`flux uri`, so the
   ``jobid:`` prefix is optional. *PATH* is a hierarchical path expression
   that may contain an optional leading slash (``/``) (which references
   the top-level, root instance explicitly), followed by zero or more job
   IDs separated by slashes. The special IDs ``.`` and ``..`` indicate
   the current instance (within the hierarchy) and its parent, respectively.
   This allows resolution of a single job running in the current instance
   via ``f1234``, explicitly within the root instance via ``/f2345``, or
   a job running within another job via ``f3456/f789``. Completely relative
   paths can also be used such as ``..`` to get the URI of the current
   parent, or ``../..`` to get the URI of the parent's parent. Finally,
   a single slash (``/``) may be used to get the root instance URI.

   The ``jobid`` scheme supports the optional query parameter ``?wait``, which
   causes the resolver to wait until a URI has been posted to the job eventlog
   for the target jobs(s), if the job is in RUN state or any previous state.
   Note that the resolver could wait until the job is inactive if the ``?wait``
   query parameter is used on a job that is not an instance of Flux.

pid:PID
  This scheme attempts to read the :envvar:`FLUX_URI` value from the
  process id *PID* using ``/proc/PID/environ``. If *PID* refers to a
  :program:`flux-broker`, then the scheme reads :envvar:`FLUX_URI` from the
  broker's initial program or another child process since :envvar:`FLUX_URI`
  in the broker's environment would refer to *its* parent (or may not be
  set at all in the case of a test instance started with :option:`flux
  start --test-size=N`).

slurm:JOBID
  This scheme makes a best-effort to resolve the URI of a Flux instance
  launched under Slurm. It invokes :program:`srun` to run :program:`scontrol
  listpids` on the first node of the job, and then uses the ``pid``
  resolver until it finds a valid :envvar:`FLUX_URI`.


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

Get the URI of the root instance from within a job running at any depth:

::

   $ flux uri /
   local:///run/flux/local

Get the URI of a local flux-broker

::

   $ flux uri pid:$(pidof -s flux-broker)
   local:///tmp/flux-sLuBkZ/local-0

The following submits a batch job and returns the instance URI once it is
available. Without the :option:`--wait` flag, :command:`flux uri` would fail
immediately with "job is not running":

::

   $ flux uri --wait $(flux batch -n1 --wrap flux run myprogram)


Get the URI for a Flux instance running as a Slurm job:

::

   $ flux uri slurm:7843494
   ssh://cluster42/var/tmp/user/flux-MpnytT/local-0


RESOURCES
=========

.. include:: common/resources.rst
