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

verify
   (optional) Boolean or table controlling resource verification behavior at
   broker startup. When resources are verified, the actual resources discovered
   via HWLOC are compared against the configured resources. Verification
   failures cause the broker rank to drain itself with an appropriate message.

   If set to a boolean:

   - ``true``: Enable strict verification for all resources (hostname, cores,
     and GPUs)
   - ``false``: Disable verification for all resources (set default="ignore")

   If set to a table, each resource type can be configured with one of four
   verification modes:

   - ``"strict"``: Drain rank on any mismatch (missing or extra resources)
   - ``"allow-extra"``: Drain only on missing resources
   - ``"allow-missing"``: Drain only on extra resources
   - ``"ignore"``: Don't verify this resource type

   The table supports a ``default`` key that sets the mode for all resource
   types, and individual resource type keys (``hostname``, ``core``, ``gpu``)
   that override the default.

   .. note::

    The default configuration when no ``resource.verify`` table is
    explicitly set allows extra resources and ignores GPUs to preserve
    backward compatibility with older versions of Flux. The implicit
    configuration is as follows::

     [resource.verify]
     default = "allow-extra"
     gpu = "ignore"
     hostname = "strict"

    If a ``[resource.verify]`` table is provided, even if empty, the
    default behavior is strict verification for all resources::

     [resource.verify]
     default = "strict"

   .. note::

    When running as a job, resource verification is skipped by default
    unless an explicit ``resource.verify`` configuration is provided.

   Examples::

     # Enable strict verification for all resources
     [resource]
     verify = true

     # Empty verify table also enables strict verification
     [resource.verify]

     # Verify cores, but don't verify GPUs (useful when HWLOC cannot detect
     # them)
     [resource.verify]
     gpu = "ignore"

     # Allow extra cores but enforce strict GPU inventory
     [resource.verify]
     core = "allow-extra"

     # Set global default with selective overrides
     [resource.verify]
     default = "allow-missing"
     hostname = "strict"

noverify
   (optional) If true, disable the draining of nodes when there is a
   discrepancy between configured resources and HWLOC-probed resources.
   This setting overrides any setting in ``[resource.verify]``.

rediscover
   (optional) If true, force rediscovery of resources using HWLOC, rather
   then using the R and HWLOC XML from the enclosing instance.

journal-max
   (optional) An integer containing the maximum number of resource eventlog
   events held in the resource module for the ``resource.journal`` RPC. The
   default is 100,000. This value takes immediate effect on a configuration
   update.

Note that, except where noted above, updates to the resource table are
ignored until the next Flux restart.

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
