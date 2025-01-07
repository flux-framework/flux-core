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

config
   (optional) An array of resource config entries used as an alternative to
   a R object configured by ``resource.path``. Each array entry must contain
   a ``hosts`` key in RFC 29 Hostlist Format which configures the list of
   hosts to which the rest of the entry applies. The entry may also contain
   ``cores`` and/or ``gpus`` keys which configure the set of core ids and
   GPU ids (in RFC 22 idset form) available on the targeted hosts, or a
   ``properties`` key which is an array of property strings to assign to
   ``hosts``. It is not an error to list a host multiple times, instead
   each entry updates ``hosts``. If the ``config`` array exists, then any
   ``path`` is ignored.

   Example::

     [[resource.config]]
     hosts = "test[1-100]"
     cores = "0-7"

     [[resource.config]]
     hosts = "test[1,2]"
     gpus = "0-1"

     [[resource.config]]
     hosts = "test[1-89]"
     properties = ["batch"]

     [[resource.config]]
     hosts = "test[90-100]"
     properties = ["debug"]

scheduling
   (optional) Set the path to a file stored as JSON which will be used
   to amend the configured R with a RFC 20 ``scheduling`` key.

exclude
   (optional) A string value that defines one or more nodes to withhold
   from scheduling, either in RFC 22 idset form, or in RFC 29 hostlist form.

   If a drained node is subsequently excluded, the drain state of the node
   is cleared since nodes cannot be both excluded and drained.

norestrict
   (optional) Disable restricting of the loaded HWLOC topology XML to the
   current cpu affinity mask of the Flux broker. This option should be used
   when the Flux system instance is constrained to a subset of cores,
   but jobs run within this instance should have access to all cores.

noverify
   (optional) If true, disable the draining of nodes when there is a
   discrepancy between configured resources and HWLOC-probed resources.

rediscover
   (optional) If true, force rediscovery of resources using HWLOC, rather
   then using the R and HWLOC XML from the enclosing instance.

Note that updates to the resource table are ignored until the next Flux
restart.

EXAMPLE
=======

::

   [resource]
   path = "/etc/flux/system/R"
   exclude = "test[3,108]"
   norestrict = true


RESOURCES
=========

.. include:: common/resources.rst


FLUX RFC
========

:doc:`rfc:spec_22`

:doc:`rfc:spec_29`


SEE ALSO
========

:man5:`flux-config`
