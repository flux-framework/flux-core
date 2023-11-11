Per-task options
----------------

:man1:`flux-run`, :man1:`flux-submit` and :man1:`flux-bulksubmit` take two
sets of mutually exclusive options to specify the size of the job request.
The most common form uses the total number of tasks to run along with
the amount of resources required per task to specify the resources for
the entire job:

.. option:: -n, --ntasks=N

   Set the number of tasks to launch (default 1).

.. option:: -c, --cores-per-task=N

   Set the number of cores to assign to each task (default 1).

.. option:: -g, --gpus-per-task=N

   Set the number of GPU devices to assign to each task (default none).
