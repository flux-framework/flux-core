### Docker images for flux-core

The Dockerfiles, resulting docker images, and `docker-run-checks.sh`
script contained herein are used as part of the strategy for CI testing
of Flux Framework projects under [Travis CI](https://travis-ci.org).

Docker is used under Travis to speed up deployment of an
environment with correct build dependencies and to keep a docker
image deployed at `fluxrm/flux-core` DockerHub with latest master build
(`fluxrm/flux-core:latest`) and tagged builds (`fluxrm/flux-core:v<tag>`),
which can be used by other framework projects to build against the latest
or a tagged version of flux-core.

#### fluxrm/testenv Docker images

The Dockerfiles under `bionic-base/Dockerfile` and
`centos7-base/Dockerfile` describe the images built under the
`fluxrm/testenv:bionic-base` and `fluxrm/testenv:centos7-base`
respectively, and include the base dependencies required to build
flux-core. These images are updated manually by flux-core maintainers, but
the Dockerfiles should be kept up to date for a single point of management.

#### The travis build Dockerfile

A secondary Dockerfile exists under `./travis/Dockerfile` which is used
to customize the `fluxrm/testenv` before building. Without this secondary
`docker build` stage, there would be no way for PRs on GitHub to add
new dependencies for users that are not core maintainers (or the "base"
images would need to be completely rebuilt on each CI run). For now,
`flux-security` is also built manually within the `travis/Dockerfile`
because it is assumed that package will be rapidly changing, and it
would not make sense to be constantly updating the base `fluxrm/testenv`
Docker images.

#### Adding a new dependency

When constructing a PR that adds new dependency, the dependency should
be added (for both CentOS and Ubuntu) in `travis/Dockerfile`. This will
result in a temporary docker image being created during testing of the
PR with the dependency installed.

Later, a flux-core maintainer can move the dependency into the `testenv`
Docker images `bionic-base/Dockerfile` and `centos7-base/Dockerfile`.
These docker images should then be built by hand and manually
pushed to DockerHub at `fluxrm/testenv:bionic-base` and
`fluxrm/testenv:centos7-base`. Be sure to test that the `docker-run-test.sh`
script still runs against the new `testenv` images, e.g.:

```
$ for i in bionic-base centos7-base; do
    make clean &&
    docker build --no-cache -t fluxrm/testenv:$i src/test/docker/$i &&
    src/test/docker/docker-run-checks.sh -j 4 --image=$i &&
    docker push fluxrm/testenv:bionic-base
   done
```

