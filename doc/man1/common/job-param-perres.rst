Per-resource options
--------------------

The second set of options allows an amount of resources to be specified
with the number of tasks per core or node set on the command line. It is
an error to specify any of these options when using any per-task option
listed above:

.. option:: --cores=N

   Set the total number of cores.

.. option:: --tasks-per-node=N

   Set the number of tasks per node to run.

.. option:: --gpus-per-node=N

   With :option:`--nodes`, request a specific number of GPUs per node.

.. option:: --tasks-per-core=N

   Force a number of tasks per core. Note that this will run *N* tasks per
   *allocated* core. If nodes are exclusively scheduled by configuration or
   use of the :option:`--exclusive` flag, then this option could result in many
   more tasks than expected. The default for this option is effectively 1,
   so it is useful only for oversubscribing tasks to cores for testing
   purposes. You probably don't want to use this option.
