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

When constructing a PR that adds a new dependency, add it to
`checks/Dockerfile` first. This causes a temporary build of the
checks image with the new dependency for the duration of the PR, without
requiring a published `fluxrm/testenv` update before the PR can pass CI.

Once the PR is merged, move the dependency into the appropriate
`src/test/docker/<distro>/Dockerfile` files. When that change lands on
master, `testenv.yml` will automatically detect the modified Dockerfile(s)
and rebuild and push the updated `fluxrm/testenv` images to DockerHub.

Note: if a PR modifies a testenv Dockerfile directly (rather than going
through `checks/Dockerfile`), `docker-run-checks.sh` will detect the
change and build the base image locally for that CI run so tests are not
blocked waiting for a published image update.

#### Local Testing

Developers can test the docker images themselves. If new dependencies are
needed, update the relevant Dockerfile(s) under `src/test/docker/`. To
create a local Docker image, run:

```
docker build -t fluxrm/testenv:$image src/test/docker/$image
```

To test the locally created image, run:

```
src/test/docker/docker-run-checks.sh -i $image [options] -- [arguments]
```

If the testenv Dockerfile for `$image` has been modified in the current
branch, `docker-run-checks.sh` will build the base image locally
automatically before running checks.
