# Flux Python Bindings

Hello! You've found the flux Python bindings, which we currently store alongside
the source code as they are tightly integrated. Currently, the python
bindings build with source flux, and we are working on means to install separately.

## Docker

Here is a way to test installing the Python bindings. From the root of flux,
build the [Dockerfile](Dockerfile) here. Note that we are also cloning
flux security to show installing / building all modules.

```bash
$ docker build -t ghcr.io/flux-framework/flux-python -f src/bindings/python/Dockerfile .
```

This will build flux from your git repository, and install the Python bindings
with a setup.py install! You can then shell into the container:

```bash
$ docker run -it ghcr.io/flux-framework/flux-python
```

And flux should be compiled, and you can use ipython to import flux:

```bash
# ipython
> import flux
```

Note that the `make install` doesn't currently include security, as this
is a separate repository and we need to figure out how to package it also.
You can still install it, however (and instructions are below).

### Building Modules

First, here is how to build (any extension below) (without install):

```bash
$ python3 setup.py build_ext
```

And how to package into a tarball:

```bash
$ python setup.py sdist
$ ls dist/
flux-0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.1a1-py3.10-linux-x86_64.egg  flux-0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.1a1.tar.gz
```

Any of the install commands below are shown with "install," however you can change
any install to a build_ext to use instead.

### Development Libraries

You can install with development libraries:

```bash
$ pip3 install -e .[dev]
```

### Installing Modules

By default, we install core flux. However, you can use the setup.py to install
any of the following modules (in addition to the core build):

#### hostlist

```bash
$ python setup.py install --hostlist
```
And to test:
```python
import flux.hostlist
```

#### idset

```bash
$ python setup.py install --idset
```

And to test:
```python
import flux.idset
```

#### rlist

```bash
$ python setup.py install --rlist
```
I think rlist means "resource list?"  So to test:

```python
import flux.resource
```

#### security

For Flux Security, the security include and source directories are required, since we couldn't
know where you built it!

```bash
$ python setup.py install --security --security-include=/usr/local/include/flux/security --security-src=/code/security
```

Or just go NUTS

```bash
$ python3 setup.py install --hostlist --rlist --idset --security --security-include=/usr/local/include/flux/security --security-src=/code/security
```

And that's it! We still have other (modules?) to compile, and can do that next.

## Development

It's sometimes helpful to work interactively - e.g., instead of needing to rebuild with every change,
you can bind the python directory on your host to the container:

```bash
$ docker run -v $PWD/src/bindings/python:/code/src/bindings/python -it ghcr.io/flux-framework/flux-python
```

This means that changes you make on your host will appear in your container, and then you can
manually run the build:

```bash
$ cd src/bindings/python
$ python3 setup.py install
```

And install development libraries (they are also added as an extra `flux[dev]`.

```bash
$ pip3 install -r requirements-dev.txt
```

And the setup.py takes most things as variables, so you can customize variables it would be run as follows:

```bash
$ python setup.py install --flux-root=/path/to/flux
```

## Using Pip

The setup.py exports environment variables for the build script to see:

 - FLUX_INSTALL_ROOT: where flux was cloned
 - FLUX_SECURITY_INCLUDE: the security includes directory (if installing with `--security`)
 - FLUX_SECURITY_SOURCE: the source code of security.

Pip doesn't seem to see environment variables, unfortunately, so if you install with pip you need
to export them yourself (instead of providing on the command line):

```bash
# Install development requirements
$ FLUX_INSTALL_ROOT=/code pip3 install .[dev]

# Add install options
$ FLUX_INSTALL_ROOT=/code pip3 install --install-option="--name=value" .
```

And you can see setup.py for other install options. I'm not sure this is considered good practice
to run additional commands in the script, but I'm not comfortable refactoring into a different
setup routine before I know what's going on, and to do that I want to keep the logic 
relatively close to the original.
