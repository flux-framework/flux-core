FROM ubuntu:focal

LABEL maintainer="Stephen Herbein <herbein1@llnl.gov>"

# avoid debconf from asking for input
ENV DEBIAN_FRONTEND noninteractive

# Update pkg caches, install latest pkg utils:
RUN apt-get update \
 && apt-get -qq install -y --no-install-recommends \
        apt-utils \
 && rm -rf /var/lib/apt/lists/*

# Ubuntu's minimal image installs a bonus `man` binary, breaking some of our
# tests (i.e., `flux help foo`).  Remove their wrapper and install the real
# thing, without installing every man page under the sun (which the `unminimize`
# command would do)
RUN rm -f /usr/bin/man && dpkg-divert --quiet --remove --rename /usr/bin/man \
    && apt-get update \
    && apt-get -qq install -y --no-install-recommends man-db \
    && rm -rf /var/lib/apt/lists/*

# Utilities
RUN apt-get update \
 && apt-get -qq install -y --no-install-recommends \
        locales \
        ca-certificates \
        wget \
        man \
        git \
        sudo \
        vim \
        munge \
        lcov \
        ccache \
        lua5.2 \
        lua-posix \
        valgrind \
        jq \
 && rm -rf /var/lib/apt/lists/*

# Compilers, autotools
RUN apt-get update \
 && apt-get -qq install -y --no-install-recommends \
        build-essential \
        pkg-config \
        autotools-dev \
        libtool \
        autoconf \
        automake \
        make \
        cmake \
        clang \
        clang-tidy \
 && rm -rf /var/lib/apt/lists/*

# Python
RUN apt-get update \
 && apt-get -qq install -y --no-install-recommends \
        python3-dev \
        python3.8-dev \
        python3-pip \
        python3-setuptools \
        python3-wheel \
        python3-cffi \
        python3-six \
        python3-yaml \
        python3-jsonschema \
 && rm -rf /var/lib/apt/lists/*

# Sphinx packages for docs
RUN python3 -m pip install sphinx sphinx-rtd-theme sphinxcontrib-spelling

RUN mkdir -p /usr/lib/python3.8/dist-packages \
 && echo ../site-packages >/usr/lib/python3.8/dist-packages/site-packages.pth

# Other deps
RUN apt-get update \
 && apt-get -qq install -y --no-install-recommends \
        libsodium-dev \
        libzmq3-dev \
        libjansson-dev \
        libmunge-dev \
        libncurses-dev \
        liblua5.2-dev \
        lua-posix-dev \
        liblz4-dev \
        uuid-dev \
        libhwloc-dev \
        libopenmpi-dev \
        libs3-dev \
        libarchive-dev \
        libpam-dev \
 && rm -rf /var/lib/apt/lists/*

# Testing utils and libs
RUN apt-get update \
 && apt-get -qq install -y --no-install-recommends \
        faketime \
        libfaketime \
        pylint \
        cppcheck \
        enchant \
        aspell \
        aspell-en \
 && rm -rf /var/lib/apt/lists/*

RUN locale-gen en_US.UTF-8
ENV LANG=C.UTF-8

# Install caliper by hand for now:
RUN mkdir caliper \
 && cd caliper \
 && wget -O - https://github.com/LLNL/Caliper/archive/v1.7.0.tar.gz | tar xvz --strip-components 1 \
 && mkdir build \
 && cd build \
 && CC=gcc CXX=g++ cmake .. -DCMAKE_INSTALL_PREFIX=/usr -DWITH_GOTCHA=Off \
 && make -j 4 \
 && make install \
 && cd ../.. \
 && rm -rf caliper
