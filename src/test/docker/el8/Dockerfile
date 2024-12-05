FROM rockylinux:8

LABEL maintainer="Mark Grondona <mgrondona@llnl.gov>"

#  Enable PowerTools for development packages
RUN yum -y update \
 && dnf -y install 'dnf-command(config-manager)' \
 && yum config-manager --set-enabled powertools \
 && yum -y update \
#  Enable EPEL
 && yum -y install epel-release \
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
	valgrind \
	jq \
	which \
	file \
	vim \
	patch \
	diffutils \
#  Compilers, autotools
	pkgconfig \
	libtool \
	autoconf \
	automake \
	gcc \
	gcc-c++ \
	clang \
	clang-tools-extra \
	make \
	ninja-build \
	cmake \
	bison \
	flex \
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
	sqlite-devel \
	libuuid-devel \
	hwloc-devel \
	lua-devel \
	valgrind-devel \
	libs3-devel \
	systemd-devel \
	libarchive-devel \
	pam-devel \
#  Other deps
	perl-Time-HiRes \
	lua-posix \
	libfaketime \
	cppcheck \
	enchant \
	aspell \
	aspell-en \
	glibc-langpack-en \
	hwloc \
 && yum clean all

#  Set default /usr/bin/python to python3
RUN alternatives --set python /usr/bin/python3

# Install catch by hand for now:
COPY scripts/fetch-and-build-catch.sh /fetch-and-build-catch.sh
RUN /fetch-and-build-catch.sh

# Install mvapich2
RUN mkdir mvapich2 \
 && cd mvapich2 \
 && wget -O - http://mvapich.cse.ohio-state.edu/download/mvapich/mv2/mvapich2-2.3.6.tar.gz | tar xvz --strip-components 1 \
 && ./configure --with-device=ch3:sock --disable-fortran --prefix=/usr \
 && make -j4 \
 && make install \
 && cd .. \
 && rm -rf mvapich2

# Install lcov
RUN rpm --nodeps -i http://downloads.sourceforge.net/ltp/lcov-1.14-1.noarch.rpm

# Install Python 3 coverage
RUN pip3 install coverage

ENV LANG=C.UTF-8
RUN printf "LANG=C.UTF-8" > /etc/locale.conf

COPY src/test/docker/el9/config.site /usr/share/config.site
