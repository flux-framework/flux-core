FROM fedora:35

LABEL maintainer="Mark Grondona <mgrondona@llnl.gov>"

#  Enable PowerTools for development packages
RUN yum -y update \
 && yum -y update \
#  Utilities
 && yum -y install \
	wget \
	man-db \
	less \
	git \
	sudo \
	munge \
	ccache \
	lua \
	mpich \
	valgrind \
	jq \
	which \
	file \
	vim \
	patch \
	diffutils \
	hostname \
	flex \
#  Compilers, autotools
	pkgconfig \
	libtool \
	autoconf \
	automake \
	gcc \
	gcc-c++ \
	libasan \
	make \
	cmake \
#  Python
	python36 \
	python3-devel \
	python3-cffi \
	python3-six \
	python3-yaml \
	python3-jsonschema \
	python3-sphinx \
#  Development dependencies
	libsodium-devel \
	zeromq-devel \
	jansson-devel \
	munge-devel \
	ncurses-devel \
	lz4-devel \
	libuuid-devel \
	hwloc-devel \
	mpich-devel \
	lua-devel \
	valgrind-devel \
	libs3-devel \
	libarchive-devel \
	pam-devel \
	libevent-devel \
#  Other deps
	perl-Time-HiRes \
	lua-posix \
	libfaketime \
	cppcheck \
	enchant \
	aspell \
	aspell-en \
	glibc-langpack-en \
	lcov \
	hwloc \
 && yum clean all

#  Add /usr/bin/mpicc link so MPI tests are built
RUN alternatives --install /usr/bin/mpicc mpicc /usr/lib64/mpich/bin/mpicc 100

# Install caliper by hand for now:
RUN mkdir caliper \
 && cd caliper \
 && wget -O - https://github.com/LLNL/Caliper/archive/v1.7.0.tar.gz | tar xvz --strip-components 1 \
 && mkdir build \
 && cd build \
 && cmake .. -DCMAKE_INSTALL_PREFIX=/usr -DWITH_GOTCHA=off \
 && make -j 4 \
 && make install \
 && cd ../.. \
 && rm -rf caliper

# Install openpmix, prrte
RUN mkdir prrte \
 && cd prrte \
 && git clone https://github.com/openpmix/openpmix.git \
 && git clone https://github.com/openpmix/prrte.git \
 && ls -l \
 && set -x \
 && cd openpmix \
 && git checkout fefaed568f33bf86f28afb6e45237f1ec5e4de93 \
 && ./autogen.pl \
 && ./configure --prefix=/usr --disable-static && make -j 4 install \
 && ldconfig \
 && cd .. \
 && cd prrte \
 && git checkout 477894f4720d822b15cab56eee7665107832921c \
 && ./autogen.pl \
 && ./configure --prefix=/usr && make -j 4 install \
 && cd ../.. \
 && rm -rf prrte

ENV LANG=C.UTF-8
RUN printf "LANG=C.UTF-8" > /etc/locale.conf

COPY config.site /usr/share/config.site
