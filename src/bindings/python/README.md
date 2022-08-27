# Flux Python Bindings

Hello! You've found the flux Python bindings, which we currently store alongside
the source code as they are tightly integrated. Currently, the python
bindings build with source flux, and we are working on means to install separately.

## Docker

Here is a way to test installing the Python bindings. From the root of flux,
build the [Dockerfile](Dockerfile) here:

```bash
$ docker build -t ghcr.io/flux-framework/flux-python -f src/bindings/python/Dockerfile .
```

And then shell into the container:

```bash
$ docker run -it ghcr.io/flux-framework/flux-python
```

And flux should be compiled. You can cd to the Python bindings and build.

```bash
$ python3 setup.py install
```

And to customize variables it would be run as follows:

```bash
$ python setup.py install --path=/path/to/flux
$ pip install --install-option="--path=/path/to/flux" .
```

And you can see setup.py for other install options. I'm not sure this is considered good practice
to run additional commands in the script, but I'm not comfortable refactoring into a different
setup routine before I know what's going on, and to do that I want to keep the logic 
relatively close to the original.
