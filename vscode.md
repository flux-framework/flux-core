[Dev Container](https://code.visualstudio.com/docs/remote/containers)
provided via the assets in [.devcontainer](https://code.visualstudio.com/docs/remote/containers#_create-a-devcontainerjson-file).

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

