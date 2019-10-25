[![Build Status](https://travis-ci.org/flux-framework/flux-core.svg?branch=master)](https://travis-ci.org/flux-framework/flux-core)
[![Coverage Status](https://coveralls.io/repos/flux-framework/flux-core/badge.svg?branch=master&service=github)](https://coveralls.io/github/flux-framework/flux-core?branch=master)

_NOTE: The interfaces of flux-core are being actively developed
and are not yet stable._ The github issue tracker is the primary
way to communicate with the developers.

See also the flux-framework.org [Online Documentation](http://flux-framework.org/docs/home/).

### flux-core

flux-core implements the communication layer and lowest level
services and interfaces for the Flux resource manager framework.
It consists of a distributed message broker, plug-in _comms modules_
that implement various distributed services, and an API and set
of utilities to utilize these services.

flux-core is intended to be the first building block used in the
construction of a site-composed Flux resource manager.  Other building
blocks are also in development under the
[flux-framework github organization](https://github.com/flux-framework),
including a fully functional workload
[scheduler](https://github.com/flux-framework/flux-sched).

Framework projects use the C4 development model pioneered in
the ZeroMQ project and forked as
[Flux RFC 1](https://github.com/flux-framework/rfc/blob/master/spec_1.adoc).
Flux licensing and collaboration plans are described in
[Flux RFC 2](https://github.com/flux-framework/rfc/blob/master/spec_2.adoc).
Protocols and API's used in Flux will be documented as Flux RFC's.

#### Build Requirements

flux-core requires the following packages to build:

**redhat**	| **ubuntu** 		| **version**		| **note**
---------- 	| ---------- 		| -----------		| --------
autoconf	| autoconf		|			|
automake	| automake		|			|
libtool		| libtool		|			|
libsodium-devel	| libsodium-dev		| >= 1.0.14		|
zeromq4-devel	| libzmq3-dev		| >= 4.0.4		|
czmq-devel	| libczmq-dev		| >= 3.0.1		|
jansson-devel	| libjansson-dev	| >= 2.6		|
lz4-devel	| liblz4-dev		|			|
hwloc-devel	| libhwloc-dev		| >= v1.11.1, < 2.0	|
sqlite-devel	| libsqlite3-dev	| >= 3.0.0		|
lua		| lua5.1		| >= 5.1, < 5.3		|
lua-devel	| liblua5.1-dev		| >= 5.1, < 5.3		|
lua-posix 	| lua-posix             | 			| *1*
python-devel	| python-dev		| >= 2.7		|
python-cffi	| python-cffi		| >= 1.1		|
python-six	| python-six		| >= 1.9		|
python-yaml	| python-yaml		| >= 3.10.0		|
python-jsonschema | python-jsonschema	| >= 2.3.0		|
asciidoc	| asciidoc         	| 			| *2*
asciidoctor	| asciidoctor         	| >= 1.5.7		| *2*
aspell		| aspell		|			| *3*
valgrind	| valgrind		|			| *3*
mpich		| mpich			|			| *3*
jq		    | jq			|			| *3*

*Note 1 - Due to a packaging issue, Ubuntu lua-posix may need the
following symlink (true for version 33.4.0-2):*
```
$ sudo ln -s posix_c.so /usr/lib/x86_64-linux-gnu/lua/5.1/posix.so
```

*Note 2 - only needed if optional man pages are to be created.  Only one
of asciidoc or asciidoctor is needed.  Asciidoc is used if both are installed.*

*Note 3 - optional, for enabling additional tests*.

```
./autogen.sh   # skip if building from a release tarball
./configure
make
make check
```
#### Bootstrapping a Flux instance

A Flux instance is composed of a set of `flux-broker` processes
that boostrap via PMI (e.g. under another resource manager), or locally
via the `flux start` command.

No administrator privilege is required to start a Flux instance
as described below.

##### Single node session

To start a Flux instance (size = 8) on the local node for testing:
```
src/cmd/flux start --size 8
```
A shell is spawned that has its environment set up so that Flux
commands can find the message broker socket.  When the shell exits,
the session exits.

##### SLURM session

To start a Flux instance (size = 64) on a cluster using SLURM,
first ensure that MUNGE is set up on your cluster, then:
```
srun --pty --mpi=none -N64 src/cmd/flux start
```
The srun --pty option is used to connect to the rank 0 shell.
When you exit this shell, the session terminates.

#### Flux commands

Within a session, the path to the `flux` command associated with the
session broker will be prepended to `PATH`, so use of a relative or
absolute path is no longer necessary.

To see a list of commonly used commands run `flux` with no arguments,
`flux help`, or `flux --help`
```
$ flux help
Usage: flux [OPTIONS] COMMAND ARGS
  -h, --help             Display this message.
  -v, --verbose          Be verbose about environment and command search

Common commands from flux-core:
   broker             Invoke Flux comms message broker daemon
   content            Access instance content storage
   cron               Schedule tasks on timers and events
   dmesg              manipulate broker log ring buffer
   env                Print or run inside a Flux environment
   event              Send and receive Flux events
   exec               Execute processes across flux ranks
   get,set,lsattr     Access, modify, and list broker attributes
   hwloc              Control/query resource-hwloc service
   keygen             generate keys for Flux security
   kvs                Flux key-value store utility
   logger             create a Flux log entry
   module             manage Flux extension modules
   ping               measure round-trip latency to Flux services
   proxy              Create proxy environment for Flux instance
   ps                 List subprocesses managed by brokers
   start              bootstrap a local Flux instance
   submit             submit job requests to a scheduler
   user               Flux user database client
```

Most of these have UNIX manual pages as `flux-<sub-command>(1)`,
which can also be accessed using `./flux help <sub-command>`.

#### A note about PMI

When flux is launched, it requires PMI-1 in order to bootstrap.
It can use PMI-1 in one of two ways, by inheriting a file descriptor
via the `PMI_FD` environment variable, or by dlopening a PMI library.
The library name is `libpmi.so`, unless overridden by the `PMI_LIBRARY`
environment variable.  If a PMI library is not found, flux falls back
to "singleton" operation, where each broker is an independent flux instance.
The PMI bootstrap may be traced by setting the `FLUX_PMI_DEBUG` environment
variable.

When flux launches flux or an MPI job, it provides PMI-1 to bootstrap the
MPI's runtime.  It offers a PMI server and sets the `PMI_FD` environment
variable to point to an open file descriptor connected to it.  It also offers
a `libpmi.so` library that can be dlopened.

If your system process manager uses PMIx, the `libpmi.so` compatibility library
provided by the PMIx project should be sufficient to bootstrap flux.
If your version of PMIx was not built with the compatibility libraries
installed, you may build libpmix as a separate package to get them installed.

#### Release

SPDX-License-Identifier: LGPL-3.0

LLNL-CODE-76440
