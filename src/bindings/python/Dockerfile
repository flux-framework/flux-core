FROM ubuntu:22.04

# used in configure.ac
ARG FLUX_VERSION=0.42.0
ENV FLUX_VERSION=${FLUX_VERSION}
ENV DEBIAN_FRONTEND=noninteractive
# from root:
# docker build -t ghcr.io/flux-framework/flux-python -f src/bindings/python/Dockerfile .
# docker run -it ghcr.io/flux-framework/flux-python

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
        libtool \
        git \
        build-essential \
        # Flux security
        libjson-glib-1.0.0 \ 
        libjson-glib-dev \ 
        libpam-dev && \
        ldconfig && \
        rm -rf /var/lib/apt/lists/*

WORKDIR /code

# Add flux-security directory
RUN git clone https://github.com/flux-framework/flux-security /code/security && \
    cd /code/security && \
    ./autogen.sh && \    
    ./configure --prefix=/usr/local && \
    make && \
    make install && \
    ldconfig

COPY . /code

RUN chmod +x etc/gen-cmdhelp.py && \
    ./autogen.sh && \
    ./configure --prefix=/usr/local && \
    make && \
    make install && \
    ldconfig

# Here is how to install Python manually (and see README there)
# WORKDIR /code/src/bindings/python
# RUN python3 setup.py install && ldconfig

WORKDIR /code
ENTRYPOINT ["/bin/bash"]
