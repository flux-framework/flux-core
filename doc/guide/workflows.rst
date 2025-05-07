
.. _workflows:

#########
Workflows
#########


Flux is designed for scientific :term:`workflows <workflow>`.
A workflow naturally maps to a Flux instance, with orchestration of its jobs
by its :term:`initial program`.  Since a Flux instance is also a job, Flux
workflows are highly composable.  Because Flux runs as a parallel job under
other resource managers such as Slurm, Flux workflows can be pleasantly
portable compared to other approaches.

Workflow orchestration can be a simple batch script that runs jobs in
sequence or with simple job dependencies, or it can be a more sophisticated
workflow application like
`Maestro <https://maestrowf.readthedocs.io/en/latest/Maestro/index.html>`_.

A workflow orchestrator can make full use of Flux's distributed services
such as its key-value store.

**************
KVS Guidelines
**************

The Flux KVS provides a distributed, *eventually consistent*, persistent
data store.  It supports atomic commits and synchronization via messages.
The :doc:`kvs` design document describes it in more detail.

The KVS is highly scalable for some use cases, such as sharing data with many
processes in a large parallel job.  However, it is not always the preferred
way to store workflow data.  The following guidelines may be helpful for
understanding how to use it effectively.

.. note::

  This section is under construction and is currently pretty sparse!
  For now, please open an issue or discussion in the flux-core github
  repo if you have specific KVS questions or would like to discuss your
  workflow storage and synchronization requirements.

- The default location for the KVS backing store is ``/tmp`` on the first
  node of the Flux instance's allocation.  On some systems, this may be
  a ramdisk with limited space.  See the description of ``statedir`` in
  :man7:`flux-broker-attributes` for info on redirecting the KVS backing
  store to another location.

- By default a batch job's KVS content is cleaned up when the instance
  terminates.  Use the :option:`flux batch --dump` option to preserve KVS
  content.

- Job data is stored in the KVS under the ``job`` directory.  The jobs
  themselves may store data in the KVS directory assigned to their job.
  This convention is described in :doc:`rfc:spec_16`.

- The KVS backing storage requirements go way up if there is significant
  churn in content, since every change percolates to the root of the hash
  tree and all versions of the tree are preserved.  Rewriting a key many
  times during execution may be considered an anti-pattern.

- Keys with huge values can cause head of line blocking in the broker,
  where the transfer of a large KVS message through a shared channel
  delays other messages.  A parallel file system is a better place to
  store big data.

- :man1:`flux-archive` may be helpful in some use cases, especially where
  the data becomes input to subsequent jobs because then the `stage-in`
  job shell plugin can be used.
