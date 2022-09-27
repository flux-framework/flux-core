# Flux Core on Ubuntu

We provide a [Dockerfile](Dockerfile) to build an ubuntu base image,
and a [Dockerfile.test](Dockerfile.test) to use this image to run `make check`.

### Usage

Build the main `flux-ubuntu` image:

```bash
$ docker build -f etc/docker/ubuntu/Dockerfile -t ghcr.io/flux-framework/flux-ubuntu .
```

If you want to shell inside (and bind for re-compile and development):

```bash
$ docker run -it -v $PWD:/code ghcr.io/flux-framework/flux-ubuntu
```

And then tests:

```bash
$ docker build -f etc/docker/ubuntu/Dockerfile.test -t ghcr.io/flux-framework/flux-ubuntu-test .
```
