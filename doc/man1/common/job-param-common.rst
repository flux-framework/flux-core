Common resource options
-----------------------

These commands take the following common resource allocation options:

.. option:: -N, --nodes=N

   Set the number of nodes to assign to the job. Tasks will be distributed
   evenly across the allocated nodes, unless the per-resource options
   (noted below) are used with *submit*, *run*, or *bulksubmit*. It is
   an error to request more nodes than there are tasks. If unspecified,
   the number of nodes will be chosen by the scheduler.

.. option:: -x, --exclusive

   Indicate to the scheduler that nodes should be exclusively allocated to
   this job. It is an error to specify this option without also using
   :option:`--nodes`. If :option:`--nodes` is specified without
   :option:`--nslots` or :option:`--ntasks`, then this option will be enabled
   by default and the number of tasks or slots will be set to the number of
   requested nodes.
