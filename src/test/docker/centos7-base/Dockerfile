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
      coreutils \
      ccache \
      cppcheck \
      czmq-devel \
      hwloc-devel \
      jansson-devel \
      sqlite-devel \
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
      python34-devel \
      python34-cffi \
      python34-six \
      ruby \
      sqlite \
      yaml-cpp-devel \
      valgrind \
      man-db \
      aspell \
      aspell-en \
      devtoolset-7-make \
 && yum clean all

# The cmake from yum is incredibly ancient, download a less ancient one
RUN wget -q --no-check-certificate https://cmake.org/files/v3.10/cmake-3.10.1-Linux-x86_64.tar.gz\
 && tar -xzf cmake-3.10.1-Linux-x86_64.tar.gz\
 && cp -fR cmake-3.10.1-Linux-x86_64/* /usr\
 && rm -rf cmake-3.10.1-Linux-x86_64\
 && rm cmake-3.10.1-Linux-x86_64.tar.gz

RUN /usr/bin/gem install asciidoctor

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

COPY config.site /usr/share/config.site
