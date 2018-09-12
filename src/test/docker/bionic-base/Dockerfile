FROM ubuntu:bionic

LABEL maintainer="Tom Scogland <scogland1@llnl.gov>"

RUN apt-get -qq update \
 && apt-get -qq install -y --no-install-recommends \
            wget \
            cmake \
            build-essential \
            sudo \
            git \
            vim \
            dh-autoreconf \
            ninja-build \
            ca-certificates \
            autotools-dev \
            autoconf \
            libtool \
            libboost-all-dev \
            libyaml-cpp-dev \
            libmunge-dev \
            munge \
            libczmq-dev \
            libjansson-dev \
            libhwloc-dev \
            lua5.2 \
            liblua5.2-dev \
            python-dev \
            python-cffi \
            python3-dev \
            python3-cffi \
            libsqlite3-dev \
            luarocks \
            ruby \
            faketime \
            libfaketime \
            cppcheck \
            clang-tidy \
            ccache \
            valgrind \
            libpmix-dev \
            libpmi-pmix-dev \
 && luarocks install luaposix \
 && /usr/bin/gem install asciidoctor \
 && rm -rf /var/lib/apt/lists/*

# NOTE: luaposix installed by rocks due to Ubuntu bug: #1752082 https://bugs.launchpad.net/ubuntu/+source/lua-posix/+bug/1752082
# NOTE: we need asciidoctor 1.5.7 to handle manpages, install with gem install

RUN useradd -ms /bin/bash flux \
 && printf "flux:flux" | chpasswd \
 && adduser flux sudo \
 && printf "flux ALL= NOPASSWD: ALL\\n" >> /etc/sudoers

USER flux
WORKDIR /home/flux
