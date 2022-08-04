# Flux Core Docker

We provide a [Dockerfile.base](Dockerfile.base) to build an ubuntu base image,
and (will provide) a `Dockerfile` to build a smaller one with a multi-stage build. 🚧️

Updated containers are built and deployed on merges to the master branch and releases.
If you want to request a build on demand, you can [manually run the workflow](https://docs.github.com/en/actions/managing-workflow-runs/manually-running-a-workflow) thanks to the workflow dispatch event.

### Usage

Here is how to build the container. Note that we build so it belongs to the same
namespace as the repository here. "ghcr.io" means "GitHub Container Registry" and
is the [GitHub packages](https://github.com/features/packages) registry that supports
 Docker images and other OCI artifacts. From the root of the repository:

```bash
$ docker build -f etc/docker/Dockerfile.base -t ghcr.io/flux-framework/flux-core-base .
```

**More will be added when the final flux container is developed**
