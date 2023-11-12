.. option:: -n, --nslots=N

   Set the number of slots requested. This parameter is required unless
   :option:`--nodes` is specified.

.. option:: -c, --cores-per-slot=N

   Set the number of cores to assign to each slot (default 1).

.. option:: -g, --gpus-per-slot=N

   Set the number of GPU devices to assign to each slot (default none).

.. option:: -N, --nodes=N

   Distribute allocated resource slots across *N* individual nodes.

.. option:: --exclusive

   With :option:`--nodes`, allocate nodes exclusively.
