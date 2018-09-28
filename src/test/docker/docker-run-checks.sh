#!/bin/bash
#
#  Build flux-core "travis" docker image and run tests, exporting
#   important environment variables to the docker environment.
#
#  Arguments here are passed directly to ./configure
#
#
# option Defaults:
IMAGE=bionic-base
FLUX_SECURITY_VERSION=0.2.0
JOBS=2

#
declare -r prog=${0##*/}
die() { echo -e "$prog: $@"; exit 1; }

#
declare -r long_opts="help,quiet,interactive,image:,flux-security-version:,jobs:,no-cache,distcheck,tag:"
declare -r short_opts="hqIdi:S:j:t:"
declare -r usage="
Usage: $prog [OPTIONS] -- [CONFIGURE_ARGS...]\n\
Build docker image for travis builds, then run tests inside the new\n\
container as the current user and group.\n\
\n\
Uses the current git repo for the build.\n\
\n\
Options:\n\
 -h, --help                    Display this message\n\
     --no-cache                Disable docker caching\n\
 -q, --quiet                   Add --quiet to docker-build\n\
 -t, --tag=TAG                 If checks succeed, tag image as NAME\n\
 -i, --image=NAME              Use base docker image NAME (default=$IMAGE)\n\
 -S, --flux-security-version=N Install flux-security vers N (default=$FLUX_SECURITY_VERSION)\n
 -j, --jobs=N                  Value for make -j (default=$JOBS)\n
 -d, --distcheck               Run 'make distcheck' instead of 'make check'\n\
 -I, --interactive             Instead of running travis build, run docker\n\
                                image with interactive shell.\n\
"

GETOPTS=`/usr/bin/getopt -u -o $short_opts -l $long_opts -n $prog -- $@`
if test $? != 0; then
    die "$usage"
fi
eval set -- "$GETOPTS"
while true; do
    case "$1" in
      -h|--help)                   echo -ne "$usage";          exit 0  ;;
      -q|--quiet)                  QUIET="--quiet";            shift   ;;
      -i|--image)                  IMAGE="$2";                 shift 2 ;;
      -S|--flux-security-version)  FLUX_SECURITY_VERSION="$2"; shift 2 ;;
      -j|--jobs)                   JOBS="$2";                  shift 2 ;;
      -I|--interactive)            INTERACTIVE=${SHELL};       shift   ;;
      -d|--distcheck)              DISTCHECK=t;                shift   ;;
      --no-cache)                  NO_CACHE="--no-cache";      shift   ;;
      -t|--tag)                    TAG="$2";                   shift 2 ;;
      --)                          shift; break;                       ;;
      *)                           die "Invalid option '$1'\n$usage"   ;;
    esac
done


TOP=$(git rev-parse --show-toplevel 2>&1) \
    || die "not inside flux-core git repository!"
which docker \
    || die "unable to find a docker binary"

CONFIGURE_ARGS="$@"

echo "Building image $IMAGE for user $USER $(id -u) group=$(id -g)"
docker build \
    ${NO_CACHE} \
    ${QUIET} \
    --build-arg OS=$IMAGE \
    --build-arg IMAGESRC="fluxrm/testenv:$IMAGE" \
    --build-arg USER=$USER \
    --build-arg UID=$(id -u) \
    --build-arg GID=$(id -g) \
    --build-arg FLUX_SECURITY_VERSION=$FLUX_SECURITY_VERSION \
    -t travis-builder:${IMAGE} \
    $TOP/src/test/docker/travis \
    || die "docker build failed"

echo "mounting $HOME as /home/$USER"
echo "mounting $TOP as /usr/src"

export JOBS
export DISTCHECK
export chain_lint

docker run --rm \
    --workdir=/usr/src \
    --volume=$HOME:/home/$USER \
    --volume=$TOP:/usr/src \
    -e CC \
    -e CXX \
    -e LDFLAGS \
    -e CFLAGS \
    -e CPPFLAGS \
    -e GCOV \
    -e CCACHE_CPP2 \
    -e CCACHE_READONLY \
    -e COVERAGE \
    -e TEST_INSTALL \
    -e CPPCHECK \
    -e DISTCHECK \
    -e chain_lint \
    -e JOBS \
    -e HOME \
    -e USER \
    ${INTERACTIVE:+--tty --interactive} \
    travis-builder:${IMAGE} \
    ${INTERACTIVE:-./src/test/travis_run.sh ${CONFIGURE_ARGS}} \
    || die "docker run failed"

if test -n "$TAG"; then
    # Re-run 'make install' in fresh image, otherwise we get all
    # the context from the build above
    docker run --name=tmp.$$ \
	--workdir=/usr/src \
        --volume=$TOP:/usr/src \
        --user="root" \
	travis-builder:${IMAGE} \
	sh -c "make install && \
               su -c 'flux keygen' flux && \
               userdel $USER" \
	|| (docker rm tmp.$$; die "docker run of 'make install' failed")
    docker commit \
	--change 'CMD "/usr/bin/flux"' \
	--change 'USER flux' \
	--change 'WORKDIR /home/flux' \
	tmp.$$ $TAG \
	|| die "docker commit failed"
    docker rm tmp.$$
    echo "Tagged image $TAG"
fi
