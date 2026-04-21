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

The following Dockerfiles each describe a `fluxrm/testenv` base image
containing the dependencies required to build and test flux-core:

| Directory   | DockerHub tag              |
|-------------|----------------------------|
| `alpine`    | `fluxrm/testenv:alpine`    |
| `bookworm`  | `fluxrm/testenv:bookworm`  |
| `el8`       | `fluxrm/testenv:el8`       |
| `el9`       | `fluxrm/testenv:el9`       |
| `el10`      | `fluxrm/testenv:el10`      |
| `fedora40`  | `fluxrm/testenv:fedora40`  |
| `focal`     | `fluxrm/testenv:focal`     |
| `jammy`     | `fluxrm/testenv:jammy`     |
| `noble`     | `fluxrm/testenv:noble`     |

All images are published as multi-arch manifests covering `linux/amd64`
and `linux/arm64`. The `bookworm` image additionally includes a
`linux/386` variant.

Images are rebuilt and pushed to DockerHub automatically by the
`testenv.yml` GitHub Actions workflow whenever a Dockerfile under
`src/test/docker/` changes on the master branch. A maintainer can also
trigger a rebuild for a specific image manually via the workflow_dispatch
UI in the GitHub Actions tab.

#### The "checks" build Dockerfile

A secondary Dockerfile exists under `./checks/Dockerfile` which builds
on top of `fluxrm/testenv` to produce the image used for CI test runs.
It installs `flux-security` from source, creates the test user account
with sudo access, and sets up MUNGE.

#### Adding a new dependency

Add the dependency directly to the appropriate
`src/test/docker/<distro>/Dockerfile` file(s). When `docker-run-checks.sh`
detects that a testenv Dockerfile was modified in the current branch
(by comparing against the most recent merge commit), it builds
`fluxrm/testenv:<distro>` locally before proceeding. This means
a PR can modify a testenv Dockerfile and pass CI without waiting
for a published image update.

Once the PR is merged to master, `testenv.yml` automatically detects
which Dockerfile(s) changed, builds and tests the new images for each
supported architecture, and publishes updated multi-arch manifests to
DockerHub.

#### Local Testing

Developers can test the docker images themselves. If new dependencies are
needed, update the relevant Dockerfile(s) under `src/test/docker/`. To
create a local Docker image, run from the repository root:

```
docker build -t fluxrm/testenv:$image -f src/test/docker/$image/Dockerfile .
```

To test the locally created image, run:

```
src/test/docker/docker-run-checks.sh -i $image [options] -- [arguments]
```
