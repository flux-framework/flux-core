FROM ubuntu:eoan

LABEL maintainer="Stephen Herbein <herbein1@llnl.gov>"

# Update pkg caches, install latest pkg utils:
RUN apt-get update \
 && apt-get -qq install -y --no-install-recommends \
	apt-utils

# Ubuntu's minimal image installs a bonus `man` binary, breaking some of our
# tests (i.e., `flux help foo`).  Remove their wrapper and install the real
# thing, without installing every man page under the sun (which the `unminimize`
# command would do)
RUN rm -f /usr/bin/man && dpkg-divert --quiet --remove --rename /usr/bin/man \
    && apt-get -qq install -y --no-install-recommends man-db

# Utilities
RUN apt-get -qq install -y --no-install-recommends \
        ca-certificates \
        wget \
        man \
        git \
        sudo \
        vim \
        munge \
        lcov \
        ccache \
        lua5.1 \
        lua-posix \
        mpich \
        valgrind \
        asciidoctor \
        jq

# Compilers, autotools
RUN apt-get -qq install -y --no-install-recommends \
        build-essential \
        pkg-config \
        autotools-dev \
        libtool \
        autoconf \
        automake \
        make \
        cmake \
        clang \
        clang-tidy

# Python
RUN apt-get -qq install -y --no-install-recommends \
        python-dev \
        python-cffi \
        python-six \
        python-yaml \
        python-jsonschema \
        python3-dev \
        python3-cffi \
        python3-six \
        python3-yaml \
        python3-jsonschema

# Other deps
RUN apt-get -qq install -y --no-install-recommends \
        libsodium-dev \
        libzmq3-dev \
        libczmq-dev \
        libjansson-dev \
        libmunge-dev \
        liblua5.1-dev \
        lua-posix-dev \
        liblz4-dev \
        libsqlite3-dev \
        uuid-dev \
        libhwloc-dev \
        libmpich-dev

# Testing utils and libs
RUN apt-get -qq install -y --no-install-recommends \
        faketime \
        libfaketime \
        pylint \
        cppcheck \
        aspell \
        aspell-en

RUN rm -rf /var/lib/apt/lists/*

# Install caliper by hand for now:
RUN mkdir caliper \
 && cd caliper \
 && wget -O - https://github.com/LLNL/Caliper/archive/v1.7.0.tar.gz | tar xvz --strip-components 1 \
 && mkdir build \
 && cd build \
 && CC=gcc CXX=g++ cmake .. -DCMAKE_INSTALL_PREFIX=/usr \
 && make -j 4 \
 && make install \
 && cd ../.. \
 && rm -rf caliper
