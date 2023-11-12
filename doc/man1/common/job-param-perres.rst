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
