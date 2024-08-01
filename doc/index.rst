Flux Core
=========

The flux-core project provides the distributed messaging capability and core
services and APIs of the Flux resource manager framework.  Flux-core has been
under active development since 2012, primarily at Lawrence Livermore National
Laboratory.  The flux-core project implements several of Flux's innovations:

Recursive launch
  A Flux instance can be launched standalone for testing, by systemd as a
  system service, or as a parallel job under most HPC resource managers and
  launchers, including Flux itself.  A Flux batch job or interactive allocation
  is an independent Flux instance running on a subset of the parent's
  resources.  Resources can be recursively subdivided ad infinitum for
  performance or isolation.

Small Security Footprint
  Flux-core does not contain any code that runs as root.  A Flux instance
  only requires a setuid helper (provided by the flux-security project) in
  multi-user configurations.  Flux can be deployed for single user use without
  administrator access.

Reactive Messaging
  Flux components are primarily single threads that communicate only by
  exchanging messages through message brokers.  C and Python APIs enable
  the creation of robust, distributed services that use this paradigm.

Related Information
===================

Although able to function on its own, flux-core's capability is enhanced when
combined with other framework projects.  Those wishing to deploy Flux as a
full-featured resource management solution or understand Flux in a broader
context may benefit from the following material:

.. list-table::
   :header-rows: 1

   * - Description
     - Links

   * - Main Flux Documentation Pages
     - `Docs <https://flux-framework.readthedocs.io/en/latest/index.html>`_

   * - Flux as the primary resource manager on an HPC cluster
     - `Flux Administrator's Guide <https://flux-framework.readthedocs.io/projects/flux-core/en/latest/guide/admin.html>`_

   * - Flux Request for Comments Specifications
     - `RFC <https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/index.html>`_

For high performance computing users, the key framework projects are
currently:

.. list-table::
   :header-rows: 1

   * - Framework Project
     - Links

   * - flux-core - Base Flux framework
     - `Github <https://github.com/flux-framework/flux-core>`_

       `Docs <https://flux-framework.readthedocs.io/projects/flux-core>`_ - You are already here!

   * - flux-sched - Fluxion graph-based scheduler

       Required for complex resource types and scheduling beyond FIFO

     - `Github <https://github.com/flux-framework/flux-sched>`_

       `Docs <https://flux-framework.readthedocs.io/projects/flux-sched>`_

   * - flux-security - Job request signatures and setuid helper

       Required for multi-user Flux instances

     - `Github <https://github.com/flux-framework/flux-security>`_

       `Docs <https://flux-framework.readthedocs.io/projects/flux-security>`_

   * - flux-accounting - Bank accounting and fair share priority
     - `Github <https://github.com/flux-framework/flux-accounting>`_

       `Flux Accounting Guide <https://flux-framework.readthedocs.io/en/latest/guides/accounting-guide.html>`_

   * - flux-pmix - OpenMPI support
     - `Github <https://github.com/flux-framework/flux-pmix>`_

   * - flux-coral2 - Cray MPI and rabbit storage support
     - `Github <https://github.com/flux-framework/flux-coral2>`_

       `CORAL2: Flux on Cray Shasta <https://flux-framework.readthedocs.io/en/latest/tutorials/lab/coral2.html>`_

Table of Contents
=================

.. toctree::
   :maxdepth: 2

   guide/build
   guide/support
   guide/start
   guide/interact
   guide/admin
   guide/glossary
   index_man
   python/index
   guide/internals
