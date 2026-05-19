=============================
flux-config-fake-resources(5)
=============================


DESCRIPTION
===========

The ``fake-resources`` table activates the ``fake-resources`` modprobe rc1
task (see :man1:`flux-modprobe`), which encodes a synthetic R from the
table's contents and installs it in the KVS before the resource module loads.
The result is a Flux instance that reports an arbitrary fake cluster shape,
without the underlying hardware to back it.

The intended use is testing — scaling studies, scheduler benchmarks,
integration and performance tests for tools — at sizes that would be
prohibitive on real hardware. Fake broker ranks are forced to appear as
up, but have no network endpoints, so flux-shell cannot launch real jobs
against them; use the ``system.exec.test.run_duration`` jobspec attribute
for mock execution.


KEYS
====

nnodes (required)
   Integer number of synthetic nodes to encode. Must be positive.

cores-per-node (optional)
   Integer number of cores on each synthetic node. (Default: ``64``).

gpus-per-node (optional)
   Integer number of GPUs on each synthetic node. ``0`` omits the GPU
   resource type from R entirely. (Default: ``0``).

host-prefix (optional)
   String hostname prefix. Nodes are named ``{prefix}0``, ``{prefix}1``,
   and so on. (Default: ``"fake"``).

hwloc-xml-path (optional)
   Path to an hwloc XML file describing per-node topology. When set, R is
   encoded from the XML rather than from the numeric ``cores-per-node`` and
   ``gpus-per-node`` keys, producing an R with topology structure suitable
   for locality-aware scheduling.

amend-r (optional)
   Reference to a Python callable that mutates the encoded R before it is
   written to the KVS. Two forms are accepted:

   * ``module.path:function_name`` — importlib loads the named module and
     looks up the function. Useful when the amender ships with a Python
     package on PYTHONPATH (e.g. a Fluxion JGF helper).
   * Any value without a ``:`` is interpreted as a filesystem path; the
     file is loaded as a Python module and its ``amend`` callable is used.
     Useful when the amender is not available in an installed package.

   The callable signature is ``amend(R, hwloc_xml=None) -> R``. Used to
   inject scheduler-specific metadata into R (e.g. Fluxion JGF keys,
   node properties) at broker startup, alongside the synthetic resource
   shape.


EXAMPLE
=======

::

   [fake-resources]
   nnodes = 1000
   cores-per-node = 48
   gpus-per-node = 4
   host-prefix = "node"

The equivalent invocation as ``flux start`` arguments - no config file
required::

   flux start \
       --conf=fake-resources.nnodes=1000 \
       --conf=fake-resources.cores-per-node=48 \
       --conf=fake-resources.gpus-per-node=4 \
       --conf=fake-resources.host-prefix=node \
       -- flux resource info

An example using ``amend-r`` to inject scheduler-specific R metadata from
a local Python file::

   flux start \
       --conf=fake-resources.nnodes=100 \
       --conf=fake-resources.amend-r=./my-amender.py \
       --conf=modules.alternatives.sched=sched-fluxion-qmanager \
       -- flux resource info


AMENDERS
========

An *amender* is a Python callable that mutates the encoded R before it is
written to the KVS. Use one to inject metadata that the bare
``flux R encode`` output doesn't carry — most commonly JGF for Fluxion, but
also things like node properties or scheduler-specific attributes.

An amender has the following signature:

.. code-block:: python

   def amend(R, hwloc_xml=None):
       # mutate R in place, or build a new dict; return it
       R["scheduling"] = {...}
       return R

The arguments are:

* ``R`` — the parsed R dict produced by ``flux R encode``, ready to mutate.
  The amender owns it for the duration of the call: either mutate in place
  and return the same dict, or build a new one and return that.
* ``hwloc_xml`` — when the ``hwloc-xml-path`` config key was set, this is
  the file's loaded contents as a string; otherwise it is None. Useful for
  amenders that derive their additions from the topology.

The return value is what gets written to ``resource.R`` in the KVS.

The ``amend-r`` config key accepts two forms:

* ``module.path:function_name`` — the named module is imported and the
  named function is looked up. Use this when the amender ships in a Python
  package on PYTHONPATH.
* Anything without a colon is treated as a filesystem path; the file is
  loaded as a Python module and its top-level ``amend()`` function is used.

A minimal example amender, suitable for the path form, that tags every R
with a marker in the scheduling key:

.. code-block:: python

   # my-amender.py
   def amend(R, hwloc_xml=None):
       R["scheduling"] = {"writer": "my:amender", "tag": "test"}
       return R

See :class:`flux.testing.fake_resources.InjectFakeResources` for the
underlying API and additional examples.


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man5:`flux-config`, :man1:`flux-modprobe`, :man1:`flux-start`
