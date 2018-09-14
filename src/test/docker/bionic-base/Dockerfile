FROM ubuntu:bionic

LABEL maintainer="Tom Scogland <scogland1@llnl.gov>"

# The staggered install is intentional, to get all sources
RUN apt-get update \
 && apt-get -qq install -y --no-install-recommends \
            wget \
            ca-certificates \
            cmake \
            build-essential \
            sudo \
            git \
            vim \
            dh-autoreconf \
            autotools-dev \
            autoconf \
            libtool \
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
 && rm -rf /var/lib/apt/lists/*

RUN luarocks install luaposix
RUN /usr/bin/gem install asciidoctor

RUN apt-get update \
 && apt-get -qq install -y \
            pkg-config \
            libpmix-dev \
            libpmi-pmix-dev \
	    mpich \
            libsodium-dev \
            uuid-dev  \
 && sudo update-alternatives --set mpi /usr/include/mpich \
 && rm -rf /var/lib/apt/lists/*

# Install flux-security by hand for now:
RUN wget https://github.com/flux-framework/flux-security/releases/download/v0.2.0/flux-security-0.2.0.tar.gz \
 && tar xvfz flux-security-0.2.0.tar.gz \
 && cd flux-security-0.2.0 \
 && ./configure --prefix=/usr \
 && make -j 4 \
 && make install \
 && cd .. \
 && rm -rf flux-security-0.2.0 flux-security-0.2.0.tar.gz

# NOTE: luaposix installed by rocks due to Ubuntu bug: #1752082 https://bugs.launchpad.net/ubuntu/+source/lua-posix/+bug/1752082
# NOTE: we need asciidoctor 1.5.7 to handle manpages, install with gem install

# Fix pmix link bug in ubuntu bionic package
RUN rm /usr/lib/x86_64-linux-gnu/pmix/lib/libpmix.so \
 && ln -s /usr/lib/x86_64-linux-gnu/pmix/lib/libpmix.so.2 /usr/lib/x86_64-linux-gnu/pmix/lib/libpmix.so

RUN useradd -ms /bin/bash flux \
 && printf "flux:flux" | chpasswd \
 && adduser flux sudo \
 && printf "flux ALL= NOPASSWD: ALL\\n" >> /etc/sudoers

USER flux
WORKDIR /home/flux
