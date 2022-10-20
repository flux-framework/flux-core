FROM ubuntu:22.04

# used in configure.ac
ARG FLUX_VERSION=0.42.0
ENV FLUX_VERSION=${FLUX_VERSION}
ENV DEBIAN_FRONTEND=noninteractive
# from root: 
# docker build -t ghcr.io/flux-framework/flux-ubuntu -f etc/docker/ubuntu/Dockerfile .
# docker run -it ghcr.io/flux-framework/flux-ubuntu

WORKDIR /code
COPY . /code

RUN ./etc/docker/ubuntu/install_deps.sh

RUN python3 -m pip install cffi pyaml jsonschema

RUN chmod +x etc/gen-cmdhelp.py && \
    ./autogen.sh && \
    ./configure --prefix=/usr/local && \
    make && \
    make install && \
    ldconfig

# Ensure we can find Python
RUN ln -s /usr/bin/python3 /usr/bin/python
ENTRYPOINT ["/bin/bash"]
