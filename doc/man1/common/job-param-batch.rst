Batch job options
-----------------

:man1:`flux-batch` and :man1:`flux-alloc` do not launch tasks directly, and
therefore job parameters are specified in terms of resource slot size
and number of slots. A resource slot can be thought of as the minimal
resources required for a virtual task. The default slot size is 1 core.

.. option:: -n, --nslots=N

   Set the number of slots requested. This parameter is required.

.. option:: -c, --cores-per-slot=N

   Set the number of cores to assign to each slot (default 1).

.. option:: -g, --gpus-per-slot=N

   Set the number of GPU devices to assign to each slot (default none).
