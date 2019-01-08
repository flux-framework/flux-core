FROM ubuntu:bionic

LABEL maintainer="Tom Scogland <scogland1@llnl.gov>"

# Update pkg caches, install latest pkg utils:
RUN apt-get update \
 && apt-get -qq install -y --no-install-recommends \
	apt-utils

# Utilities
RUN apt-get -qq install -y --no-install-recommends \
        ca-certificates \
        wget \
        man \
        git \
        sudo \
        vim \
        luarocks \
        ruby \
        munge \
        lcov \
        ccache \
        lua5.2 \
        mpich \
        valgrind

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
        clang-6.0 \
        clang-tidy \
        gcc-8 \
        g++-8

# Python
RUN apt-get -qq install -y --no-install-recommends \
        python-dev \
        python-cffi \
        python-six \
	python-yaml \
        python3-dev \
        python3-cffi \
        python3-six \
	python3-yaml

# Other deps
RUN apt-get -qq install -y --no-install-recommends \
        libsodium-dev \
        libzmq3-dev \
        libczmq-dev \
        libyaml-cpp-dev \
        libjansson-dev \
        libmunge-dev \
        liblua5.2-dev \
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

# NOTE: luaposix installed by rocks due to Ubuntu bug: #1752082 https://bugs.launchpad.net/ubuntu/+source/lua-posix/+bug/1752082
RUN luarocks install luaposix

# NOTE: we need asciidoctor 1.5.7 to handle manpages, install with gem install
RUN /usr/bin/gem install asciidoctor

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
