#!/bin/bash
#
#  Tag flux-core and fluxorama docker images

log() { echo "docker-deploy: $@" >&2; }
die() { log "$@"; exit 1; }

if test "$GITHUB_REPOSITORY" != "flux-framework/flux-core"; then
    log "not in flux-framework/flux-core repo, exiting..."
    exit 0
fi

test -n "$DOCKER_REPO" ||     die "DOCKER_REPO not set"
test -n "$DOCKER_PASSWORD" || die "DOCKER_PASSWORD not set"
test -n "$DOCKER_USERNAME" || die "DOCKER_USERNAME not set"
test -n "$DOCKER_TAG" ||      die "DOCKER_TAG not set"

echo $DOCKER_PASSWORD | docker login -u "$DOCKER_USERNAME" --password-stdin

log "docker push ${DOCKER_TAG}"
docker push ${DOCKER_TAG}

#  If this is the bookworm build, then also tag without image name:
if echo "$DOCKER_TAG" | grep -q "bookworm"; then
    t="${DOCKER_REPO}:${GITHUB_TAG:-latest}"
    log "docker push ${t}"
    docker tag "$DOCKER_TAG" ${t} && docker push ${t}
fi

#  If this is the el8 build, then build fluxorama image
if echo "$DOCKER_TAG" | grep -q "el8"; then
    FLUXORAMA="fluxrm/fluxorama"
    docker build -t ${FLUXORAMA} src/test/docker/fluxorama
    docker push ${FLUXORAMA}
    if test -n "$GITHUB_TAG"; then
        t=${FLUXORAMA}:${GITHUB_TAG}
        log "docker push ${t}"
        docker tag ${FLUXORAMA} ${t} && docker push ${t}
    fi
fi
