FROM alpine:3.18

LABEL maintainer="Mark A. Grondona <mgrondona@llnl.gov>"

# Update pkg caches, install latest pkg utils and basics:
RUN apk add \
    coreutils \
    procps \
    shadow \
    bash \
    sudo \
    gcc \
    g++ \
    autoconf \
    automake \
    libtool \
    make \
    cmake \
    musl-dev \
    musl-dbg \
    python3-dev \
    python3 \
    py3-cffi \
    py3-yaml \
    py3-jsonschema \
    py3-ply \
    py3-setuptools \
    py3-sphinx \
    lua5.1-dev \
    lua5.1-posix \
    czmq-dev \
    jansson-dev \
    hwloc-dev \
    lz4-dev \
    ncurses-dev \
    libarchive-dev \
    libsodium-dev \
    linux-pam-dev \
    git \
    jq \
    aspell \
    aspell-en \
    valgrind

RUN wget https://github.com/dun/munge/releases/download/munge-0.5.15/munge-0.5.15.tar.xz \
 && tar -xf munge-0.5.15.tar.xz \
 && cd munge-0.5.15 \
 && ./configure --prefix=/usr --sysconfdir=/etc || cat config.log \
 && make -j 4 \
 && make install \
 && cd .. \
 && rm -rf munge-*

RUN groupadd -r munge \
 && useradd -d /etc/munge -g munge -s /sbin/nologin -r munge

ENV LANG=C.UTF-8

# Install catch by hand for now:
COPY scripts/fetch-and-build-catch.sh /fetch-and-build-catch.sh
RUN /fetch-and-build-catch.sh
