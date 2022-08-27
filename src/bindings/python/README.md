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

## Development

These are commands I ran to try and emulate what was happening in Makefile.am,
while interactively in the Docker container above. Here is how I bound setup.py
to work on it:

```bash
$ docker run -v $PWD/setup.py /code/src/bindings/python/setup.py -it ghcr.io/flux-framework/flux-python
```

And then made changes to add logic from the previous harder parsing script into setup.py and tested like:

```bash
$ python3 setup.py install
```

And it would be run as follows:

```bash
$ python setup.py install --path=/path/to/flux
$ pip install --install-option="--path=/path/to/flux" .
```
And you can see setup.py for other install options. I'm not sure this is considered good practice
to run additional commands in the script, but I'm not comfortable refactoring into a different
setup routine before I know what's going on, and to do that I want to keep the logic 
relatively close to the original.
