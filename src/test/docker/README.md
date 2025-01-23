### Docker images for flux-core

The Dockerfiles, resulting docker images, and `docker-run-checks.sh`
script contained herein are used as part of the strategy for CI testing
of Flux Framework projects.

Docker is used under CI to speed up deployment of an
environment with correct build dependencies and to keep a docker
image deployed at `fluxrm/flux-core` DockerHub with latest master build
(`fluxrm/flux-core:latest`) and tagged builds (`fluxrm/flux-core:v<tag>`),
which can be used by other framework projects to build against the latest
or a tagged version of flux-core.

#### fluxrm/testenv Docker images

The Dockerfiles `jammy/Dockerfile`, `focal/Dockerfile`,
`el7/Dockerfile`, and `el8/Dockerfile` describe the images built
under the `fluxrm/testenv:jammy`, `fluxrm/testenv:focal`,
`fluxrm/testenv:el7`, and `fluxrm/testenv:el8` respectively, and
include the base dependencies required to build flux-core. These images
are updated manually by flux-core maintainers, but the Dockerfiles should
be kept up to date for a single point of management.

#### The "checks" build Dockerfile

A secondary Dockerfile exists under `./checks/Dockerfile` which is used
to customize the `fluxrm/testenv` before building. Without this secondary
`docker build` stage, there would be no way for PRs on GitHub to add
new dependencies for users that are not core maintainers (or the "base"
images would need to be completely rebuilt on each CI run). For now,
`flux-security` is also built manually within the `checks/Dockerfile`
because it is assumed that package will be rapidly changing, and it
would not make sense to be constantly updating the base `fluxrm/testenv`
Docker images.

#### Adding a new dependency

When constructing a PR that adds new dependency, the dependency should
be added (for both rh/el and Ubuntu) in `checks/Dockerfile`. This will
result in a temporary docker image being created during testing of the
PR with the dependency installed.

Later, a flux-core maintainer can move the dependency into the `testenv`
Docker images `jammy/Dockerfile` and `el7/Dockerfile`.
These docker images should then be built by hand and manually
pushed to DockerHub at `fluxrm/testenv:jammy` and
`fluxrm/testenv:el7`. Be sure to test that the `docker-run-test.sh`
script still runs against the new `testenv` images, e.g.:

```
$ for i in focal el7 el8 fedora33 fedora34 fedora35 fedora38; do
    make clean &&
    docker build --no-cache -t fluxrm/testenv:$i src/test/docker/$i &&
    src/test/docker/docker-run-checks.sh -j 4 --image=$i &&
    docker push fluxrm/testenv:$i
  done
```

#### Bookworm and Jammy multiarch images

Building the images for linux/amd64, linux/arm64 and linux/386 requires the
Docker buildx extensions, see

 https://www.docker.com/blog/multi-arch-build-and-images-the-simple-way/

and run
```
$  docker buildx build --push --platform=linux/arm64,linux/amd64 --tag fluxrm/testenv:jammy -f src/test/docker/jammy/Dockerfile .
$  docker buildx build --push --platform=linux/386,linux/amd64,linux/arm64 --tag fluxrm/testenv:bookworm -f src/test/docker/bookworm/Dockerfile .
```

to build and push images to docker hub.

#### Local Testing

Developers can test the docker images themselves. If new dependencies are needed,
they can update the `$image` Dockerfiles manually (where `$image` is one of jammy, el7, el8, or focal).
To create a local Docker image, run the command:

```
docker build -t fluxrm/testenv:$image src/test/docker/$image
```

To test the locally created image, run:

```
src/test/docker/docker-run-checks.sh -i $image [options] -- [arguments]
```
