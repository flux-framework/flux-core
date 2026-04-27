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

declare -r long_opts="help,no-home,no-cache,rebuild,jobs:,image:,interactive"
declare -r short_opts="hrj:i:I"
declare -r usage="
Usage: $prog [OPTIONS]\n\
Build fluxorama system test docker image for CI builds\n\
\n\
Options:\n\
 -h, --help                    Display this message\n\
 -j, --jobs=N                  Value for make -j (default=$JOBS)\n\
 -i, --image=NAME              Base image (default=$IMAGE)\n\
 -I, --interactive             Run interactive shell in container\n\
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
      -I|--interactive) INTERACTIVE="-ti";       shift   ;;
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

# Force memory and cpuset controllers to be delegated in Github actions only
# also remove apparmor configs for unix-chkpwd and sudo which seem to get in
# way of starting user managers in the container, and use of passwordless sudo
# necessary for testing.
# (Avoid modifying user systems by default)
if test "$GITHUB_ACTIONS" = "true"; then
    # Test: disable unix-chkpwd on host in github actions
    sudo apparmor_parser -R /etc/apparmor.d/unix-chkpwd
    sudo apparmor_parser -R /etc/apparmor.d/sudo

    for controller in memory cpuset; do
        grep -qw "$controller" /sys/fs/cgroup/cgroup.subtree_control \
          || echo "+$controller" | sudo tee /sys/fs/cgroup/cgroup.subtree_control
    done
fi

# Now check for controllers in subtree_control and warn if not present.
# All resource control testing will fail without these controllers delegated:
for controller in memory cpuset; do
  if ! grep -qw "$controller" /sys/fs/cgroup/cgroup.subtree_control; then
      echo "::warning:: cgroup controller '$controller' not delegated on host," \
           "sdexec constraints may not work"
  fi
done

if test "$REBUILD_BASE_IMAGE" = "t"; then
    checks_group "Rebuilding fluxrm/flux-core:$IMAGE from source" \
      $TOP/src/test/docker/docker-run-checks.sh \
        -j $JOBS \
        -i el8 \
        -t $IMAGE \
        --install-only
fi

# Usage: podman_pull IMAGE
# Pull image from docker if missing or checksum out of date
podman_pull() {
  DOCKER_ID=$(docker inspect --format '{{.Id}}' $1 2>/dev/null)
  PODMAN_ID=$(sudo podman inspect --format '{{.Id}}' $1 2>/dev/null)
  if [ "$DOCKER_ID" != "$PODMAN_ID" ]; then
      checks_group "Moving $IMAGE from docker to podman" \
          sudo podman pull docker-daemon:$1
  fi
}

podman_pull $IMAGE

checks_group "Building system image for user $USER $(id -u) group=$(id -g)" \
  sudo podman build \
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
  sudo podman run -d --rm \
    --name=flux-system-test-$$ \
    --privileged \
    --systemd=always \
    --volume=/sys/fs/cgroup:/sys/fs/cgroup:rw \
    --security-opt apparmor=unconfined \
    --hostname=fluxorama \
    --network=host \
    --workdir=$WORKDIR \
    $MOUNT_HOME_ARGS \
    --volume=$TOP:$WORKDIR \
    --mount=type=tmpfs,destination=/test/tmpfs-1m,tmpfs-size=1048576 \
    fluxorama:systest \
    || die "docker run of fluxorama test container failed"


# Wait up to 3 minutes for Flux to come up in the container
# Dump logs for diagnosis in case it fails.
NAME=flux-system-test-$$
flux_uid=$(sudo podman exec $NAME id -u flux)
TIMEOUT=180
i=0
while [ $i -lt $TIMEOUT ]; do
  sudo podman exec $NAME systemctl is-active --quiet flux.service && break
  i=$((i + 5))
  echo "--- flux.service status at ${i}s ---"
  sudo podman exec $NAME systemctl status flux.service --no-pager -l 2>&1 \
    | tail -20
  sudo podman exec $NAME systemctl status "user@${flux_uid}.service" \
      --no-pager -l 2>&1 | tail -20
  if [ $i -ge $TIMEOUT ]; then
      echo "=== TIMEOUT: full journal ==="
      sudo podman exec $NAME journalctl --no-pager -n 200
      checks_die "flux.service failed to start within ${TIMEOUT}s"
  fi
  sleep 5
done

#  Start user@uid.service service for unit tests:
#
checks_group "Starting user service user@$(id -u).service" \
  sudo podman exec flux-system-test-$$ \
    systemctl start user@$(id -u).service \
    || die "podman start user@$(id -u).service failed"


# We want checks to fail if cgroups aren't properly delegated in Github Actions
# Check here and error out if user.slice doesn't have controllers delegated:
if test "$GITHUB_ACTIONS" = "true"; then
  for controller in memory cpuset; do \
    sudo podman exec $NAME \
      grep -qw "$controller" /sys/fs/cgroup/user.slice/cgroup.subtree_control \
      || checks_die "cgroup controller '$controller' not delegated in " \
                    "container user.slice, sdexec constraints will not work"; \
  done
fi

if test -n "$INTERACTIVE"; then
  msg="Executing interactive shell in system instance container"
else
  msg="Executing tests under system instance container"
fi
checks_group "$msg" \
  sudo podman exec \
    "${INTERACTIVE}" \
    -u $USER:$GID \
    ${CC+-e CC=$CC} \
    ${CXX+-e CXX=$CXX} \
    ${LDFLAGS+-e LDFLAGS=$LDFLAGS} \
    ${CFLAGS+-e CFLAGS=$CFLAGS} \
    ${CPPFLAGS+-e CPPFLAGS=$CPPFLAGS} \
    -e PS1=$PS1 \
    -e GCOV=$GCOV \
    -e CCACHE_CPP2=$CCACHE_CPP2 \
    -e CCACHE_READONLY=$CCACHE_READONLY \
    -e COVERAGE=$COVERAGE \
    -e TEST_INSTALL=$TEST_INSTALL \
    -e CPPCHECK=$CPPCHECK \
    -e DISTCHECK=$DISTCHECK \
    -e RECHECK=$RECHECK \
    -e UNIT_TEST_ONLY=$UNIT_TEST_ONLY \
    -e chain_lint=$chain_lint \
    -e JOBS=$JOBS \
    -e USER=$USER \
    -e PROJECT=$PROJECT \
    -e CI=$CI \
    -e TAP_DRIVER_QUIET=$TAP_DRIVER_QUIET \
    -e FLUX_TEST_TIMEOUT=$FLUX_TEST_TIMEOUT \
    -e FLUX_TEST_SIZE_MAX=$FLUX_TEST_SIZE_MAX \
    -e FLUX_ENABLE_SYSTEM_TESTS=t \
    -e PYTHON_VERSION=$PYTHON_VERSION \
    -e PRELOAD=$PRELOAD \
    -e POISON=$POISON \
    -e INCEPTION=$INCEPTION \
    -e ASAN_OPTIONS=$ASAN_OPTIONS \
    -e BUILD_DIR=$BUILD_DIR \
    -e HOME=/home/$USER \
    -e XDG_RUNTIME_DIR=/run/user/$(id -u) \
    -e DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/$(id -u)/bus \
    -e SYSTEM=t \
    -w $WORKDIR \
    flux-system-test-$$ "$@"
RC=$?

sudo podman exec -ti flux-system-test-$$ shutdown -r now

if test $RC -ne 0; then
    die "system tests failed with rc=$RC"
fi

# vi: ts=4 sw=4 expandtab
