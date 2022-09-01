=======================
flux-config-resource(5)
=======================


DESCRIPTION
===========

The Flux **resource** service provides an interface to the scheduler
for acquiring instance resources.  It accepts some configuration, especially
useful in the Flux system instance where resources must be known before
all brokers are online.

The ``resource`` table may contain the following keys:


KEYS
====

path
   (optional) Set the path to an RFC 20 (R version 1) resource description
   which defines the resources available to the system.  If undefined,
   resources are determined either by dynamic discovery, or by information
   passed from the enclosing Flux instance.  The ``flux R`` utility may be
   used to generate this file.

exclude
   (optional) A string value that defines one or more nodes to withhold
   from scheduling, either in RFC 22 idset form, or in RFC 29 hostlist form.
   This value may be changed on a live system by reloading the configuration
   on the rank 0 broker.  A newly excluded node will appear as "down" to
   the scheduler, but will still be used to determine satisfiability of job
   requests until the instance is restarted.

norestrict
   (optional) Disable restricting of the loaded HWLOC topology XML to the
   current cpu affinity mask of the Flux broker. This option should be used
   when the Flux system instance is constrained to a subset of cores,
   but jobs run within this instance should have access to all cores.


EXAMPLE
=======

::

   [resource]
   path = "/etc/flux/system/R"
   exclude = "test[3,108]"
   norestrict = true


RESOURCES
=========

Flux: http://flux-framework.org

RFC 22: Idset String Representation: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_22.html

RFC 29: Hostlist Format: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_29.html


SEE ALSO
========

:man5:`flux-config`
