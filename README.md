[![Build Status](https://travis-ci.org/flux-framework/flux-core.svg?branch=master)](https://travis-ci.org/flux-framework/flux-core)
[![Coverage Status](https://coveralls.io/repos/flux-framework/flux-core/badge.svg?branch=master&service=github)](https://coveralls.io/github/flux-framework/flux-core?branch=master)

_NOTE: The interfaces of flux-core are being actively developed
and are not yet stable._ The github issue tracker is the primary
way to communicate with the developers.

### flux-core

flux-core implements the communication layer and lowest level
services and interfaces for the Flux resource manager framework.
It consists of a distributed message broker and plug-in _comms modules_
that implement various distributed services.

#### Overview

A set of message broker instances are launched as a _comms session_.
Each instance has a rank numbered 0 to (size - 1).
Instances are interconnected with three overlay networks:
a k-ary tree rooted at rank 0 that is used for request/response
messages and data reductions, an event overlay that is used for
session-wide broadcasts, and a ring network that is used for debugging
requests.  Overlay networks are implemented using [ZeroMQ](http://zeromq.org).

The message broker natively supports the following services:
_logging_, which aggregates syslog-like log messages at rank 0;
_heartbeat_, which broadcasts a periodic event to synchronize
housekeeping tasks; _module loader_, which loads and unloads
comms modules; and _reparent_, which allows overlay networks to be
rewired on the fly.

flux-core also includes the following services implemented as
comms modules: _kvs_, a distributed key-value store;  _live_,
a service that monitors overlay network health and can rewire around
failed broker instances; _modctl_, a distributed module control service;
_barrier_, a MPI-like barrier implementation; _api_, a routing service
for clients connecting to a broker instance via a UNIX domain socket;
and _wreck_ a remote execution service.

A number of utilities are provided for accessing these services,
accessible via the `flux` command front-end (see below),

Experimental programming abstractions are provided for various recurring
needs such as data reduction, multicast RPC, streaming I/O, and others.
A PMI implementation built on the Flux KVS facilitates scalable MPI launch.
A set of Lua bindings permits rapid development of Flux utilities and test
drivers.

flux-core is intended to be the first building block used in the
construction of a site-composed Flux resource manager.  Other building
blocks are being worked on and will appear in the
[flux-framework github organization](http://github.com/flux-framework)
as they get going.

Framework projects use the C4 development model pioneered in
the ZeroMQ project and forked as
[Flux RFC 1](http://github.com/flux-framework/rfc/blob/master/spec_1.adoc).
Flux licensing and collaboration plans are described in
[Flux RFC 2](http://github.com/flux-framework/rfc/blob/master/spec_2.adoc).
Protocols and API's used in Flux will be documented as Flux RFC's.

#### Building flux-core

flux-core requires the following packages to build:
```
zeromq4-devel >= 4.0.4   # built --with-libsodium
czmq-devel >= 3.0.1
munge-devel
json-c-devel
lua-5.1-devel
luaposix
libhwloc-devel >= v1.11.0
# for python bindings
python-devel >= 2.7
python-cffi >= 1.1
# for man pages
asciidoc     
```
Spec files for building zeromq4 and czmq packages on a RHEL 6 based
system are provided for your convenience in foreign/rpm.

If you want to build the MPI-based test programs, make sure that
`mpicc` is in your PATH before you run configure.  These programs are
not built if configure does not find MPI.

```
./autogen.sh   # skip if building from a release tarball
./configure
make
make check
```
#### Bootstrapping a Flux comms session

A Flux comms session is composed of a set of `flux-broker` processes
that boostrap via PMI (e.g. under another resource manager), or locally
via the `flux start` command.

No administrator privilege is required to start a Flux comms
session as described below.

##### Single node session

To start a Flux comms session (size = 8) on the local node:
```
src/cmd/flux start --size 8
```
A shell is spawned that has its environment set up so that Flux
commands can find the message broker socket.  When the shell exits,
the session exits.

##### SLURM session

To start a Flux comms session (size = 64) on a cluster using SLURM,
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
$ flux --help
Usage: flux [OPTIONS] COMMAND ARGS
...

The flux-core commands are:
   help          Display manual for a sub-command
   keygen        Generate CURVE keypairs for session security
   start         Bootstrap a comms session interactively
   kvs           Access the Flux the key-value store
...
```

Most of these have UNIX manual pages as `flux-<sub-command>(1)`,
which can also be accessed using `./flux help <sub-command>`.
