FROM python:3.9

# used in configure.ac
ARG FLUX_VERSION=0.42.0
ENV ${FLUX_VERSION}=${FLUX_VERSION}

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
        libpam-dev && \
        ldconfig && \
        rm -rf /var/lib/apt/lists/*

WORKDIR /code
COPY . /code

# Flux needs to find "system python" here or burps an error
RUN ln -s $(which python) /usr/bin/python && \
    chmod +x etc/gen-cmdhelp.py && \
    ./autogen.sh && \
    ./configure --prefix=/usr/local --without-python && \
    make && \
    make install

# Prepare to emulate having separately
COPY ./src/bindings/python /code/src/bindings/python
WORKDIR /code/src/bindings/python
RUN pip3 install --upgrade pip && \
    pip3 install -r requirements.txt && \
    pip3 install -r requirements-dev.txt && \
    python3 setup.py install
ENTRYPOINT ["/bin/bash"]
