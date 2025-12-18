Building Releases
=================

Version Numbering
-----------------

Flux-core releases are numbered with
`Semantic Versioning <https://semver.org/>`_, where MAJOR.MINOR.PATCH release
numbers communicate that:

- major releases may break API

- minor releases add functionality in a backward compatible manner

- patch releases make backward compatible bug fixes

At this time, the project is at major version zero, indicating interfaces
are not yet stable.  That said, flux-core developers try to minimize
disruption to early adopters and announce any necessary breaking changes
in the release notes.

Obtaining Releases
------------------

Tarballs and release notes are available on the
`Github releases page <https://github.com/flux-framework/flux-core/releases>`_.

Releases for all Flux framework projects are also announced on the
`Flux Framework releases page <https://flux-framework.org/releases/>`_.

Installing Dependencies
-----------------------

Several external software packages are prerequisites for building Flux.
Scripts that install these packages for debian and redhat based distros are
located in the flux-core source tree ``scripts`` sub-directory.

The following packages are optional and may be omitted if you do not require
the associated functionality:

.. list-table::
   :header-rows: 1

   * - Package
     - Functionality

   * - `flux-security <https://github.com/flux-framework/flux-security>`_
     - Launching work as multiple users.

       For example when Flux is to be the native resource manager on a cluster.

   * - Sphinx
     - Building man pages and documentation

   * - MPI
     - Test only: sanity tests that Flux can launch MPI programs

   * - valgrind
     - Test only: checks for memory errors

Configuring and Building a Release
----------------------------------

flux-core uses GNU autotools internally, so it supports the usual
`Use Cases for the GNU Build System <https://www.gnu.org/software/automake/manual/html_node/Use-Cases.html>`_.  A standard build follows this pattern:

.. code-block:: console

  $ tar xzf flux-core-X.Y.Z.tar.gz
  $ cd flux-core-X.Y.Z
  $ ./configure --with-flux-security
  $ make
  $ make check
  $ make install

Configure *should* abort if any required build dependencies are missing or
insufficient.  Configure options options may be listed with ``./configure
--help``.

``make -j N`` may be used to speed up the build and check targets by
increasing parallelism.

All checks are expected to pass, although some timing related test defects
may cause tests to sporadically fail on slow systems or when run with too much
parallelism.  ``make recheck`` re-runs any failing checks.

Packages
--------

Source RPM packages for TOSS 4 (RHEL 8 based) are made available in
the release assets published on github.  Sites deploying a Flux system
installation are strongly encouraged to use these packages, if possible.
Source RPM packages for TOSS 5 (RHEL 9 based) are available upon request.

deb packages for Debian or Ubuntu can be built from a release tarball with
``make deb``, producing debs in the ``debbuild`` sub-directory.  This target
is used by some Flux team members to build packages for test clusters running
the `Raspberry Pi OS <https://www.raspberrypi.com/software/>`_ (Debian/GNU 11).

Docker Images
-------------

Docker images for tagged releases as well as a current development snapshot
are available in the `fluxrm/flux-core Dockerhub
<https://hub.docker.com/r/fluxrm/flux-core/tags>`_.  For example, the following
downloads a debian bookworm image containing flux-core 0.54.0 and starts a
flux instance within it:

.. code-block:: console

  $ docker pull fluxrm/flux-core:bookworm-v0.54.0-amd64
  $ docker run -ti fluxrm/flux-core:bookworm-v0.54.0-amd64
  ƒ(s=1,d=0) fluxuser@080d84548cc4:~$

Spack
-----

Flux-core and its dependencies can also be built using `spack
<https://spack-tutorial.readthedocs.io/en/latest/>`_, for example:

.. code-block:: console

  $ git clone --depth=2 https://github.com/spack/spack.git
  $ cd spack
  $ spack compiler find 
  $ spack install flux-core
  $ spack find flux-core
  -- linux-ubuntu24.04-aarch64 / %c=clang@18.1.3 ------------------
  flux-core@0.73.0
  
  -- linux-ubuntu24.04-aarch64 / %c=gcc@13.3.0 --------------------
  flux-core@0.73.0
  ==> 2 installed packages
  $ . share/spack/setup-env.sh
  $ spack load flux-core
  

Some Flux developers prefer a more ephemeral Spack configuration that 
configures Spack to use only in-directory configuration and caches and 
ignore other directories and system config files. To enable that behavior,
set ``SPACK_DISABLE_LOCAL_CONFIG=1`` and 
``SPACK_USER_CACHE_PATH=$(pwd)/cache``.
