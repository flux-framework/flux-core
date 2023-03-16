#!/bin/bash
#
#  Tag flux-core and fluxorama docker images

log() { echo "docker-deploy: $@" >&2; }
die() { log "$@"; exit 1; }

if test "$GITHUB_REPOSITORY" != "flux-framework/flux-core"; then
    log "not in flux-framework/flux-core repo, exiting..."
    exit 0
fi

test -n "$DOCKER_REPO"         || die "DOCKER_REPO not set"
test -n "$DOCKER_PASSWORD"     || die "DOCKER_PASSWORD not set"
test -n "$DOCKER_USERNAME"     || die "DOCKER_USERNAME not set"
test -n "$DOCKER_TAG"          || die "DOCKER_TAG not set"
test -n "$GITHUB_TOKEN"        || die "GITHUB_TOKEN not set"
test -n "$GITHUB_ACTOR"        || die "GITHUB_ACTOR not set"
test -n "$GITHUB_PACKAGE_REPO" || die "GITHUB_PACKAGE_REPO not set"
test -n "$GITHUB_PACKAGES_TAG" || die "GITHUB_PACKAGES_TAG not set"

echo $GITHUB_TOKEN | docker login ghcr.io -u ${GITHUB_ACTOR} --password-stdin
echo $DOCKER_PASSWORD | docker login -u "$DOCKER_USERNAME" --password-stdin

log "docker push ${DOCKER_TAG}"
docker push ${DOCKER_TAG}
docker tag ${DOCKER_TAG} ${GITHUB_PACKAGES_TAG}
docker push ${GITHUB_PACKAGES_TAG}

function push_bionic() {
    URI="${1}"
    t="${URI}:${GITHUB_TAG:-latest}"
    log "docker push ${t}"
    docker tag "$DOCKER_TAG" ${t} && docker push ${t}
}

function build_fluxorama() {
    FLUXORAMA=${1}
    docker build -t ${FLUXORAMA} src/test/docker/fluxorama
    docker push ${FLUXORAMA}
    if test -n "$GITHUB_TAG"; then
        t=${FLUXORAMA}:${GITHUB_TAG}
        log "docker push ${t}"
        docker tag ${FLUXORAMA} ${t} && docker push ${t}
    fi
}

#  If this is the bionic build, then also tag without image name:
if echo "$DOCKER_TAG" | grep -q "bionic"; then

    # Docker Hub and GitHub packages
    push_bionic ${DOCKER_REPO}
    push_bionic ${GITHUB_PACKAGE_REPO}
    
fi

#  If this is the el8 build, then build fluxorama image
if echo "$DOCKER_TAG" | grep -q "el8"; then

    build_fluxorama "fluxrm/fluxorama"
    build_fluxorama "ghcr.io/flux-framework/fluxorama"

fi
