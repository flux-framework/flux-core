FROM centos:7

LABEL maintainer="Tom Scogland <scogland1@llnl.gov>"

# add EPEL so we don't have to build everything by hand
# add scl and devtoolset-7 so we can use next-rhel tools, just make 4 for now
# so we can avoid make sync issues on travis
RUN yum -y update \
 && yum -y install https://dl.fedoraproject.org/pub/epel/epel-release-latest-7.noarch.rpm \
 && yum -y install centos-release-scl-rh \
 && yum -y update \
 && yum -y install \
      which \
      sudo \
      git \
      wget \
      vim-minimal \
      autoconf \
      automake \
      libtool \
      gcc \
      gcc-c++ \
      file \
      make \
      munge \
      munge-devel \
      ncurses-devel \
      coreutils \
      ccache \
      cppcheck \
      hwloc \
      hwloc-devel \
      jansson-devel \
      uuid-devel \
      libuuid-devel \
      libfaketime \
      libsodium-devel \
      lua \
      lua-devel \
      lua-posix \
      mpich-devel \
      pkgconfig \
      python-devel \
      python-cffi \
      python-six \
      python-yaml \
      python-jsonschema \
      python36-devel \
      python36-cffi \
      python36-six \
      python36-yaml \
      python36-jsonschema \
      sqlite \
      valgrind \
      valgrind-devel \
      man-db \
      enchant \
      aspell \
      aspell-en \
      devtoolset-7-make \
      lz4-devel \
      jq \
      libarchive-devel \
      pam-devel \
 && yum clean all

# Sphinx packages for docs
RUN python3 -m pip install sphinx sphinx-rtd-theme sphinxcontrib-spelling

# The cmake from yum is incredibly ancient, download a less ancient one
RUN wget -q --no-check-certificate https://cmake.org/files/v3.10/cmake-3.10.1-Linux-x86_64.tar.gz\
 && tar -xzf cmake-3.10.1-Linux-x86_64.tar.gz\
 && cp -fR cmake-3.10.1-Linux-x86_64/* /usr\
 && rm -rf cmake-3.10.1-Linux-x86_64\
 && rm cmake-3.10.1-Linux-x86_64.tar.gz

COPY config.site /usr/share/config.site

ENV LANG=en_US.UTF-8

# Create /tmp -> /var/tmp link to ensure Flux tests work in this configuration
RUN rm -rf /tmp && ln -sf /var/tmp /tmp
