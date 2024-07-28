FROM ubuntu:noble

LABEL maintainer="Tom Scogland <scogland1@llnl.gov>"

# copy in requirements files
COPY scripts/requirements-ci.txt /requirements-ci.txt
COPY scripts/requirements-doc.txt /requirements-doc.txt

# Utilities
RUN apt-get update \
# install latest pkg utils:
 && apt-get -qq install -y --no-install-recommends \
        apt-utils \
 && apt-get -qq install -y --no-install-recommends \
        locales \
        ca-certificates \
        wget \
        man \
        git \
        flex \
        ssh \
        sudo \
        vim \
        luarocks \
        munge \
        lcov \
        ccache \
        lua5.2 \
        lua5.2-posix-dev \
        valgrind \
        jq \
# Compilers, autotools
        build-essential \
        pkg-config \
        autotools-dev \
        libtool \
        autoconf \
        automake \
        make \
        cmake \
        ninja-build \
        clang-18 \
        clang-tools-18 \
# default version
        gcc-13 \
        g++-13 \
# newest
        gcc-14 \
        g++-14 \
# Python
        libffi-dev \
## python 3.12
        python3-dev \
        python3-pip \
        python3-setuptools \
        python3-wheel \
# Other deps
        libsodium-dev \
        libzmq3-dev \
        libjansson-dev \
        libmunge-dev \
        libncursesw5-dev \
        liblua5.2-dev \
        liblz4-dev \
        libsqlite3-dev \
        uuid-dev \
        libhwloc-dev \
        libmpich-dev \
        libs3-dev \
        libevent-dev \
        libarchive-dev \
        libpam-dev \
        libpmix-dev \
# testing utils and libs
        faketime \
        libfaketime \
        pylint \
        cppcheck \
        enchant-2 \
        catch2 \
        aspell \
        aspell-en \
        time \
# Testing utils and libs
# NOTE: sudo pip install is necessary to get differentiated installations of
# python binary components for multiple python3 variants, --ignore-installed
# makes it ignore local versions of the packages if your home directory is
# mapped into the container and contains the same libraries
 && (for PY in python3.12 ; do \
        sudo $PY -m pip install --upgrade --ignore-installed --break-system-packages -r /requirements-ci.txt ; \
        sudo mkdir -p /usr/lib/${PY}/dist-packages; \
        echo ../site-packages >/tmp/site-packages.pth; \
        sudo mv /tmp/site-packages.pth /usr/lib/${PY}/dist-packages; \
    done) \
 && apt-get -qq purge -y python3-pip \
 && apt-get -qq autoremove -y \
 && rm -rf /var/lib/apt/lists/*

# Ubuntu containers now "minimize" themselves, so manpages aren't installed.
# To warn people about this, /usr/bin/man is a shell script that doesn't bother
# to look for the man page, but just prints a warning.  Link /usr/bin/man.REAL
# to /usr/bin/man to make this behave
RUN ln -sf /usr/bin/man.REAL /usr/bin/man

# Install caliper by hand for now:
COPY scripts/fetch-and-build-caliper.sh /fetch-and-build-caliper.sh
RUN /fetch-and-build-caliper.sh

ENV LANG=C.UTF-8

# the image has a UID 1000 named ubuntu now for some reason, fix it
RUN userdel ubuntu
# noble defaults to an invalid TZ /UTC
ENV TZ="America/Los_Angeles"

# noble currently packages an MPICH linked with PMIX, so we can't bootstrap it
# natively, nor can its own mpiexec, see here:
# https://bugs.launchpad.net/ubuntu/+source/mpich/+bug/2072338
# so we build our own copy of current stable
COPY scripts/fetch-and-build-mpich.sh /fetch-and-build-mpich.sh
RUN /fetch-and-build-mpich.sh

# hwloc tries to look for opengl devices  by connecting to a port that might
# sometimes be an x11 port, but more often for us is munge, turn it off
ENV HWLOC_COMPONENTS=-gl
