FROM ubuntu:jammy

LABEL maintainer="Tom Scogland <scogland1@llnl.gov>"

# Update pkg caches, install latest pkg utils:
RUN apt-get update \
 && apt-get -qq install -y --no-install-recommends \
        apt-utils \
 && rm -rf /var/lib/apt/lists/*

# Utilities
RUN apt-get update \
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
 && rm -rf /var/lib/apt/lists/*

# Ubuntu containers now "minimize" themselves, so manpages aren't installed.
# To warn people about this, /usr/bin/man is a shell script that doesn't bother
# to look for the man page, but just prints a warning.  Link /usr/bin/man.REAL
# to /usr/bin/man to make this behave
RUN ln -sf /usr/bin/man.REAL /usr/bin/man

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
        clang-15 \
        clang-tools-15 \
        gcc-12 \
        g++-12 \
 && rm -rf /var/lib/apt/lists/*

# Python
# NOTE: sudo pip install is necessary to get differentiated installations of
# python binary components for multiple python3 variants, --ignore-installed
# makes it ignore local versions of the packages if your home directory is
# mapped into the container and contains the same libraries
RUN apt-get update \
 && apt-get -qq install -y --no-install-recommends \
	libffi-dev \
        python3-dev \
        python3.11-dev \
        python3-pip \
        python3-setuptools \
        python3-wheel \
 && rm -rf /var/lib/apt/lists/*

RUN for PY in python3.10 python3.11 ; do \
        sudo $PY -m pip install --upgrade --ignore-installed \
	    "markupsafe==2.0.0" \
            coverage cffi ply six pyyaml "jsonschema>=2.6,<4.0" \
            sphinx sphinx-rtd-theme sphinxcontrib-spelling; \
	sudo mkdir -p /usr/lib/${PY}/dist-packages; \
	echo ../site-packages >/tmp/site-packages.pth; \
	sudo mv /tmp/site-packages.pth /usr/lib/${PY}/dist-packages; \
    done ; \
    apt-get -qq purge -y python3-pip \
 && apt-get -qq autoremove -y

# Other deps
RUN apt-get update \
 && apt-get -qq install -y --no-install-recommends \
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
 && rm -rf /var/lib/apt/lists/*

# Testing utils and libs
RUN apt-get update \
 && apt-get -qq install -y --no-install-recommends \
        faketime \
        libfaketime \
        pylint \
        cppcheck \
        enchant-2 \
        aspell \
        aspell-en \
        time \
 && rm -rf /var/lib/apt/lists/*

RUN locale-gen en_US.UTF-8

# NOTE: luaposix installed by rocks due to Ubuntu bug: #1752082 https://bugs.launchpad.net/ubuntu/+source/lua-posix/+bug/1752082
RUN luarocks install luaposix

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
