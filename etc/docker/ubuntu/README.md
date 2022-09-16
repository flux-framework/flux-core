# Flux Core on Ubuntu

We provide a [Dockerfile](Dockerfile) to build an ubuntu base image,
and a [Dockerfile.test](Dockerfile.test) to use this image to run `make check`.

### Usage

Build the main `flux-ubuntu` image:

```bash
$ docker build -f etc/docker/ubuntu/Dockerfile -t ghcr.io/flux-framework/flux-ubuntu .
```

And then tests:

```bash
$ docker build -f etc/docker/ubuntu/Dockerfile.test -t ghcr.io/flux-framework/flux-ubuntu-test .
```
