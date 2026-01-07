FROM fedora:34

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
 && yum clean all

#  Add /usr/bin/mpicc link so MPI tests are built
RUN alternatives --install /usr/bin/mpicc mpicc /usr/lib64/mpich/bin/mpicc 100

ENV LANG=C.UTF-8
RUN printf "LANG=C.UTF-8" > /etc/locale.conf

COPY config.site /usr/share/config.site
