FROM debian:bookworm

LABEL maintainer="Tom Scogland <scogland1@llnl.gov>"

# Update pkg caches, install latest pkg utils and basics:
RUN apt-get update \
 && apt-get -qq install -y --no-install-recommends \
        apt-utils \
        wget \
# Utilities
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
        mpich \
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
        clang-15 \
        clang-tools-15 \
        gcc-12 \
        g++-12 \
# Python
        libffi-dev \
        python3.11-dev \
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
        libevent-dev \
        libarchive-dev \
        libpam-dev \
# Testing utils and libs
        faketime \
        libfaketime \
        pylint \
        cppcheck \
        enchant-2 \
        aspell \
        aspell-en \
        time \
 && rm -rf /var/lib/apt/lists/* \
# NOTE: sudo pip install is necessary to get differentiated installations of
# python binary components for multiple python3 variants, --ignore-installed
# makes it ignore local versions of the packages if your home directory is
# mapped into the container and contains the same libraries
 ;  for PY in python3.11 ; do \
        sudo $PY -m pip install --upgrade --ignore-installed \
        --break-system-packages \
	    "markupsafe==2.0.0" \
            coverage cffi ply six pyyaml "jsonschema>=2.6,<4.0" \
            sphinx sphinx-rtd-theme sphinxcontrib-spelling; \
	sudo mkdir -p /usr/lib/${PY}/dist-packages; \
	echo ../site-packages >/tmp/site-packages.pth; \
	sudo mv /tmp/site-packages.pth /usr/lib/${PY}/dist-packages; \
    done ; \
    apt-get -qq purge -y python3-pip \
 && apt-get -qq autoremove -y

RUN locale-gen en_US.UTF-8

# NOTE: luaposix installed by rocks due to Ubuntu bug: #1752082 https://bugs.launchpad.net/ubuntu/+source/lua-posix/+bug/1752082
RUN luarocks install luaposix

# Install catch by hand for now:
COPY scripts/fetch-and-build-catch.sh /fetch-and-build-catch.sh
RUN /fetch-and-build-catch.sh

# Install openpmix, prrte
RUN mkdir prrte \
 && cd prrte \
 && git clone https://github.com/openpmix/openpmix.git \
 && git clone https://github.com/openpmix/prrte.git \
 && ls -l \
 && set -x \
 && cd openpmix \
 && git checkout fefaed568f33bf86f28afb6e45237f1ec5e4de93 \
 && ./autogen.pl \
 && ./configure --prefix=/usr --disable-static && make -j 4 install \
 && ldconfig \
 && cd .. \
 && cd prrte \
 && git checkout 477894f4720d822b15cab56eee7665107832921c \
 && ./autogen.pl \
 && ./configure --prefix=/usr && make -j 4 install \
 && cd ../.. \
 && rm -rf prrte

ENV LANG=C.UTF-8
