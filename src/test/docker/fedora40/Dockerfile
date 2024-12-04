FROM fedora:40

LABEL maintainer="Mark Grondona <mgrondona@llnl.gov>"

#  Enable PowerTools for development packages
#  Utilities
RUN dnf -y install \
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
	clang \
	clang-tools-extra \
	libasan \
	make \
	ninja-build \
	cmake \
#  Python
	python3-devel \
	python3-cffi \
	python3-six \
	python3-yaml \
	python3-jsonschema \
	python3-sphinx \
	python3-setuptools \
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
	mpich-devel \
	lua-devel \
	valgrind-devel \
	libs3-devel \
	libarchive-devel \
	pam-devel \
	pmix-devel \
	catch-devel \
#  Other deps
	perl-Time-HiRes \
	lua-posix \
	libfaketime \
	cppcheck \
	enchant \
	aspell \
	aspell-en \
	time \
	glibc-langpack-en \
	lcov \
 && dnf clean all

#  Add /usr/bin/mpicc link so MPI tests are built
RUN alternatives --install /usr/bin/mpicc mpicc /usr/lib64/mpich/bin/mpicc 100

ENV LANG=C.UTF-8
RUN printf "LANG=C.UTF-8" > /etc/locale.conf

COPY src/test/docker/fedora40/config.site /usr/share/config.site

# the psm3 connector added to libfabrics in ~1.12 causes errors when allowed to
# use non-local connectors on a system with virtual NICs, since we're in a
# docker container, prevent this
ENV PSM3_HAL=loopback
# hwloc tries to look for opengl devices  by connecting to a port that might
# sometimes be an x11 port, but more often for us is munge, turn it off
ENV HWLOC_COMPONENTS=-gl
