FROM python:3.9

ARG FLUX_RELEASE=0.42.0
ENV ${FLUX_RELEASE}=${FLUX_RELEASE}

# from root:
# docker build -t ghcr.io/flux-framework/flux-python -f src/bindings/python/Dockerfile .
# docker run -it ghcr.io/flux-framework/flux-python

RUN apt-get update \
 && apt-get -qq install -y --no-install-recommends \
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

RUN wget https://github.com/flux-framework/flux-core/releases/download/v${FLUX_RELEASE}/flux-core-${FLUX_RELEASE}.tar.gz && \
    tar -xzvf flux-core-${FLUX_RELEASE}.tar.gz && \
    mv flux-core-${FLUX_RELEASE} /code
WORKDIR /code

# Flux needs to find "system python" here or burps an error
RUN ln -s $(which python) /usr/bin/python && \
    ./configure --prefix=/usr/local --without-python && \
    rm -rf /code/src/bindings/python

# Prepare to emulate having separately
COPY ./src/bindings/python /code/src/bindings/python
WORKDIR /code/src/bindings/python
RUN pip3 install --upgrade pip && \
    pip3 install -r requirements.txt && \
    pip3 install -r requirements-dev.txt
ENTRYPOINT ["/bin/bash"]
