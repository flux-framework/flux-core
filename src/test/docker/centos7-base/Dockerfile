FROM centos:7

LABEL maintainer="Tom Scogland <scogland1@llnl.gov>"

# add EPEL so we don't have to build everything by hand
RUN yum -y update \
 && yum -y install https://dl.fedoraproject.org/pub/epel/epel-release-latest-7.noarch.rpm \
 && yum -y update \
 && yum -y install \
      sudo \
      git \
      wget \
      vim-minimal \
      autoconf \
      automake \
      gcc \
      gcc-c++ \
      libtool \
      make \
      munge \
      munge-devel \
      coreutils \
      ccache \
      cppcheck \
      czmq-devel \
      hwloc-devel \
      jansson-devel \
      uuid-devel \
      libuuid-devel \
      libfaketime \
      libsodium-devel \
      lua \
      lua-posix \
      mpich-devel \
      pkgconfig \
      pmix-devel \
      python-devel \
      python-cffi \
      ruby \
      sqlite \
      yaml-cpp-devel \
      valgrind \
 && yum clean all

RUN /usr/bin/gem install asciidoctor

# Install flux-security by hand for now:
RUN wget https://github.com/flux-framework/flux-security/releases/download/v0.2.0/flux-security-0.2.0.tar.gz \
 && tar xvfz flux-security-0.2.0.tar.gz \
 && cd flux-security-0.2.0 \
 && ./configure --prefix=/usr \
 && make -j 4 \
 && make install \
 && cd .. \
 && rm -rf flux-security-0.2.0 flux-security-0.2.0.tar.gz

# Install caliper by hand for now:
RUN mkdir caliper \
 && cd caliper \
 && wget -O - https://github.com/LLNL/Caliper/archive/v1.7.0.tar.gz | tar xvz --strip-components 1 \
 && mkdir build \
 && cd build \
 && cmake .. -DCMAKE_INSTALL_PREFIX=/usr \
 && make -j 4 \
 && make install \
 && cd ../.. \
 && rm -rf caliper

RUN useradd -ms /bin/bash flux \
 && printf "flux:flux" | chpasswd \
 && adduser flux sudo \
 && printf "flux ALL= NOPASSWD: ALL\\n" >> /etc/sudoers

USER flux
WORKDIR /home/flux
