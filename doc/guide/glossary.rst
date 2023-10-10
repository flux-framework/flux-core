Glossary
========

Here we define Flux-specific and general HPC and workload management terms
used in our documentation that may not be familiar to all readers.

  .. glossary::

     initial program
        A user-defined program, such as a batch script, launched on the first
	node of a Flux instance.  Its purpose is to launch and monitor a
	workload.  Once it is complete, the instance exits.

     system instance
        A multi-user Flux instance running as the primary resource manager
	on a cluster.
