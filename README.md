[![ci](https://github.com/flux-framework/flux-core/workflows/ci/badge.svg)](https://github.com/flux-framework/flux-core/actions?query=workflow%3A.github%2Fworkflows%2Fmain.yml)
[![codecov](https://codecov.io/gh/flux-framework/flux-core/branch/master/graph/badge.svg)](https://codecov.io/gh/flux-framework/flux-core)

See our [Online Documentation](https://flux-framework.readthedocs.io)!

_NOTE: the github issue tracker is the primary way to communicate
with Flux developers._


### flux-core

flux-core implements the lowest level services and interfaces for the Flux
resource manager framework.  It is intended to be the first building block
used in the construction of a site-composed Flux resource manager.  Other
building blocks are also in development under the
[flux-framework github organization](https://github.com/flux-framework),
including a workload [scheduler](https://github.com/flux-framework/flux-sched).

Framework projects use the C4 development model pioneered in
the ZeroMQ project and forked as
[Flux RFC 1](https://flux-framework.rtfd.io/projects/flux-rfc/en/latest/spec_1.html).
Flux licensing and collaboration plans are described in
[Flux RFC 2](https://flux-framework.rtfd.io/projects/flux-rfc/en/latest/spec_2.html).
Protocols and API's used in Flux will be documented as Flux RFC's.

#### Build Requirements

For convenience, scripts that install flux-core's build dependencies
are available for [redhat](scripts/install-deps-rpm.sh) and
[debian](scripts/install-deps-deb.sh) distros.

##### Building from Source
```
./autogen.sh   # skip if building from a release tarball
./configure
make
make check
```

##### VSCode Dev Containers

If you use VSCode we have a [Dev Container](https://code.visualstudio.com/docs/remote/containers)
provided via the assets in [.devcontainer](https://code.visualstudio.com/docs/remote/containers#_create-a-devcontainerjson-file).

<details>
  <summary>Click to expand for more information</summary>
You can follow the [tutorial](https://code.visualstudio.com/docs/remote/containers-tutorial) where you'll basically
need to:

1. Install Docker, or compatible engine
2. Install the [Development Containers](vscode:extension/ms-vscode-remote.remote-containers) extension

Then you can go to the command palette (View -> Command Palette) and select `Dev Containers: Open Workspace in Container.`
and select your cloned Flux repository root. This will build a development environment from [fluxrm/testenv](https://hub.docker.com/r/fluxrm/testenv/tags)
that are built from [src/test/docker](src/test/docker) (the focal tag) with a few tweaks to add linting and dev tools.

In addition to the usual flux dev requirements, you get:

* bear
* fd
* gdb
* GitHub CLI
* ripgrep
* and several useful vscode extensions in the vscode server instance, pre-configured for lua, c and python in flux-core


You are free to change the base image and rebuild if you need to test on another operating system!
When your container is built, when you open `Terminal -> New Terminal`, surprise! You're
in the container! The dependencies for building Flux are installed. Try building Flux - it will work without a hitch!

```bash
./autogen.sh
./configure --prefix=/usr/local
make
# This will install in the container!
sudo make install
# This will test in the container!
make check
# If you want a compilation database
make clean
./scripts/generate_compile_commands # this runs `bear make check` by default to generate for all tests as well
```

And try starting flux

```bash
flux start --test-size=4
```

Note that the above assumes installing flux to `/usr/local`. If you install elsewhere, you'll need to adjust your
`LD_LIBRARY_PATH` or similar. IPython is provided in the container for Python development, along with other linting tools.
If you ever need to rebuild, you can either restart VSCode and open in the same way (and it will give you the option)
or you can do on demand in the command palette with `Dev Containers: Rebuild Container` (with or without cache).

**Important** the development container assumes you are on a system with uid 1000 and gid 1000. If this isn't the case,
edit the [.devcontainer/Dockerfile](.devcontainer/Dockerfile) to be your user and group id. This will ensure
changes written inside the container are owned by your user. It's recommended that you commit on your system
(not inside the container) because if you need to sign your commits, the container doesn't
have access and won't be able to. If you find that you accidentally muck up permissions
and need to fix, you can run this from your terminal outside of VSCode:

```bash
$ sudo chown -R $USER .git/
# and then commit
```

</details>


#### Starting Flux

A Flux instance is composed of a set of `flux-broker` processes running as
a parallel job and can be started by most launchers that can start MPI jobs.
Doing so for a single user does not require administrator privelege.
To start a Flux instance (size = 8) on the local node for testing, use
flux's built-in test launcher:
```
src/cmd/flux start --test-size=8
```
A shell is spawned in which Flux commands can be exexcuted.  When the shell
exits, Flux exits.

For more information on starting Flux in various environments and using it,
please refer to our [docs](https://flux-framework.readthedocs.io) pages.

#### Release

SPDX-License-Identifier: LGPL-3.0

LLNL-CODE-764420
