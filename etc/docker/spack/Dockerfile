FROM ghcr.io/flux-framework/flux-core-base:latest as builder

# docker build -f etc/docker/Dockerfile -t ghcr.io/flux-framework/flux-core-ubuntu .

# The development config cannot have git@ clone, must be https
COPY ./etc/docker/spack/git-config /code/.git/config

# Get upstream tags for version (otherwise won't build)
WORKDIR /code
RUN git fetch
RUN cd /opt/flux-env && \
    . /opt/spack/share/spack/setup-env.sh && \
    spack env activate . && \
    spack install && \
    spack env view regenerate

# We can't do multistage build on a view symlink
# FROM ubuntu:22.04
# COPY --from=builder /opt/view /opt/view
# COPY --from=builder /opt/flux-view /opt/flux-view

RUN apt-get update && \
    apt-get install -y unzip gfortran python3-dev && \
    apt-get install -y libcurl4-openssl-dev libssl-dev

ENV PATH=$PATH:/opt/flux-view/bin
ENV LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/opt/flux-view/lib:/opt/flux-view/lib64
ENV DEBIAN_FRONTEND=noninteractive

# The user will see the view on shell into the container
WORKDIR /opt/
ENTRYPOINT ["/bin/bash"]
