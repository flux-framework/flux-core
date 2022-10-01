FROM ubuntu:22.04

# used in configure.ac
ARG FLUX_VERSION=0.42.0
ENV FLUX_VERSION=${FLUX_VERSION}
ENV DEBIAN_FRONTEND=noninteractive
# from root: 
# docker build -t ghcr.io/flux-framework/flux-ubuntu -f etc/docker/ubuntu/Dockerfile .
# docker run -it ghcr.io/flux-framework/flux-ubuntu

RUN apt-get update \
 && apt-get -qq install -y --no-install-recommends \
        automake \
        libsodium-dev \
        libzmq3-dev \
        libczmq-dev \
        libjansson-dev \
        libmunge-dev \
        libncursesw5-dev \
        lua5.4 \
        liblua5.4-dev \
        liblz4-dev \
        libsqlite3-dev \
        uuid-dev \
        libhwloc-dev \
        libmpich-dev \
        libs3-dev \
        libevent-dev \
        libarchive-dev \
        python3 \
        python3-dev \
        python3-pip \
        python3-sphinx \
        python3-venv \
        libtool \
        git \
        build-essential \
        # Flux security
        libjson-glib-1.0.0 \ 
        libjson-glib-dev \ 
        libpam-dev && \
        ldconfig && \
        rm -rf /var/lib/apt/lists/*

RUN python3 -m pip install cffi pyaml jsonschema
WORKDIR /code
COPY . /code

RUN chmod +x etc/gen-cmdhelp.py && \
    ./autogen.sh && \
    ./configure --prefix=/usr/local && \
    make && \
    make install && \
    ldconfig

# Ensure we can find Python
RUN ln -s /usr/bin/python3 /usr/bin/python
ENTRYPOINT ["/bin/bash"]
