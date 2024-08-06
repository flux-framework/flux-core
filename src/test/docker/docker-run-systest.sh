#!/bin/bash
#
#  Build fluxorama image with builddir mounted as /usr/src for testing.
#

PROJECT=flux-core
WORKDIR=/usr/src
MOUNT_HOME_ARGS="--volume=$HOME:/home/$USER -e HOME"
JOBS=2
IMAGE="fluxrm/flux-core:el8"

declare -r prog=${0##*/}
die() { echo -e "$prog: $@"; exit 1; }

declare -r long_opts="help,no-home,no-cache,rebuild,jobs:,image:"
declare -r short_opts="hrj:i:"
declare -r usage="
Usage: $prog [OPTIONS]\n\
Build fluxorama system test docker image for CI builds\n\
\n\
Options:\n\
 -h, --help                    Display this message\n\
 -j, --jobs=N                  Value for make -j (default=$JOBS)\n\
 -i, --image=NAME              Base image (default=$IMAGE)\n\
     --rebuild                 Rebuild base fluxorama image from source\n\
     --no-home                 Skip mounting the host home directory\n\
     --no-cache                Run docker build with --no-cache option\n\
"

# check if running in OSX
if [[ "$(uname)" == "Darwin" ]]; then
    # BSD getopt
    GETOPTS=`/usr/bin/getopt $short_opts -- $*`
else
    # GNU getopt
    GETOPTS=`/usr/bin/getopt -u -o $short_opts -l $long_opts -n $prog -- $@`
    if [[ $? != 0 ]]; then
        die "$usage"
    fi
    eval set -- "$GETOPTS"
fi

while true; do
    case "$1" in
      -h|--help)     echo -ne "$usage";          exit 0  ;;
      -j|--jobs)     JOBS="$2";                  shift 2 ;;
      -i|--image)    IMAGE="$2";                 shift 2 ;;
      --rebuild)     REBUILD_BASE_IMAGE=t;       shift   ;;
      --no-home)     MOUNT_HOME_ARGS="";         shift   ;;
      --no-cache)    NOCACHE="--no-cache";       shift   ;;
      --)            shift; break;                       ;;
      *)             die "Invalid option '$1'\n$usage"   ;;
    esac
done

if test $# -eq 0; then
    set "bash"
fi

TOP=$(git rev-parse --show-toplevel 2>&1) \
    || die "not inside $PROJECT git repository!"
which podman >/dev/null \
    || die "unable to find podman binary!"
which docker >/dev/null \
    || die "unable to find docker binary!"

. ${TOP}/src/test/checks-lib.sh

if test "$REBUILD_BASE_IMAGE" = "t"; then
    checks_group "Rebuilding fluxrm/flux-core:el8 from source" \
      $TOP/src/test/docker/docker-run-checks.sh \
        -j $JOBS \
        -i el8 \
        -t $IMAGE \
        --install-only
fi

#  Note: podman cannot pull from local docker images in GitHub actions, so
#  we have to use docker save -> podman load to make the image available
#  to podman. This is done in steps to avoid a potential issue with
#  podman reporting:
#
#  Error: payload does not match any of the supported image formats:
#
#  Saving to a file (avoiding a colon in the name) seems to work around
#  these issues.
#
checks_group "Moving $IMAGE from docker to podman" \
  docker save -o /tmp/systest-$$.tar $IMAGE \
  && ls -lh /tmp/systest-$$.tar \
  && (podman load -i /tmp/systest-$$.tar || die "podman load failed") \
  && rm -f /tmp/systest-$$.tar

#  Note: There's a bug in podman < 4 which saves an image loaded from
#  docker save as the wrong name, so we use the image digest instead:
IMAGE=$(docker images --format {{.ID}} $IMAGE)

checks_group "Building system image for user $USER $(id -u) group=$(id -g)" \
  podman build \
    ${NOCACHE} \
    --build-arg IMAGE=$IMAGE \
    --build-arg USER=$USER \
    --build-arg UID=$(id -u) \
    --build-arg GID=$(id -g) \
    -t fluxorama:systest \
    --target=systest \
    ${TOP}/src/test/docker/fluxorama \
    || die "docker build failed"

NAME=flux-system-test-$$
checks_group "Launching system instance container $NAME" \
  podman run -d --rm \
    --hostname=fluxorama \
    --workdir=$WORKDIR \
    $MOUNT_HOME_ARGS \
    --volume=$TOP:$WORKDIR \
    --volume=/sys/fs/cgroup:/sys/fs/cgroup:ro \
    --tmpfs=/run \
    --mount=type=tmpfs,destination=/test/tmpfs-1m,tmpfs-size=1048576 \
    --cap-add SYS_PTRACE \
    --name=flux-system-test-$$ \
    --network=host \
    --systemd=always \
    --userns=keep-id \
    --security-opt unmask=/sys/fs/cgroup \
    fluxorama:systest \
    || die "docker run of fluxorama test container failed"

until podman exec -u $USER:$GID \
    flux-system-test-$$ flux run hostname 2>/dev/null; do
    echo "Waiting for flux-system-test-$$ to be ready"
    sleep 1
done

#  Start user@uid.service service for unit tests:
#
checks_group "Starting user service user@$(id -u).service" \
  podman exec flux-system-test-$$ \
    systemctl start user@$(id -u).service \
    || die "podman start user@$(id -u).service failed"

if test -t 0; then
    INTERACTIVE="-ti"
fi

checks_group "Executing tests under system instance container" \
  podman exec \
    "${INTERACTIVE}" \
    -u $USER:$GID \
    ${CC+-e CC=$CC} \
    ${CXX+-e CXX=$CXX} \
    ${LDFLAGS+-e LDFLAGS=$LDFLAGS} \
    ${CFLAGS+-e CFLAGS=$CFLAGS} \
    ${CPPFLAGS+-e CPPFLAGS=$CPPFLAGS} \
    -e PS1 \
    -e GCOV \
    -e CCACHE_CPP2 \
    -e CCACHE_READONLY \
    -e COVERAGE \
    -e TEST_INSTALL \
    -e CPPCHECK \
    -e DISTCHECK \
    -e RECHECK \
    -e UNIT_TEST_ONLY \
    -e chain_lint \
    -e JOBS \
    -e USER \
    -e PROJECT \
    -e CI \
    -e TAP_DRIVER_QUIET \
    -e FLUX_TEST_TIMEOUT \
    -e FLUX_TEST_SIZE_MAX \
    -e FLUX_ENABLE_SYSTEM_TESTS=t \
    -e PYTHON_VERSION \
    -e PRELOAD \
    -e POISON \
    -e INCEPTION \
    -e ASAN_OPTIONS \
    -e BUILD_DIR \
    -e S3_ACCESS_KEY_ID \
    -e S3_SECRET_ACCESS_KEY \
    -e S3_HOSTNAME \
    -e S3_BUCKET \
    -e HOME=/home/$USER \
    -e XDG_RUNTIME_DIR=/run/user/$(id -u) \
    -e DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/$(id -u)/bus \
    -e SYSTEM=t \
    -w $WORKDIR \
    flux-system-test-$$ "$@"
RC=$?

podman exec -ti flux-system-test-$$ shutdown -r now

if test $RC -ne 0; then
    die "system tests failed with rc=$RC"
fi

# vi: ts=4 sw=4 expandtab
