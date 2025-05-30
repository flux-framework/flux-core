FROM rockylinux:9

LABEL maintainer="Mark Grondona <mgrondona@llnl.gov>"

#  Enable PowerTools for development packages
RUN dnf -y install 'dnf-command(config-manager)' \
 && dnf config-manager --enable crb \
#  Enable EPEL
 && dnf -y install epel-release \
#  Utilities
 && dnf -y install \
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
	python3-devel \
	python3-cffi \
	python3-six \
	python3-yaml \
	python3-jsonschema \
	python3-sphinx \
	python3-coverage \
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
	systemd-devel \
	libarchive-devel \
	pam-devel \
	mpich-devel \
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
	lcov \
 && dnf clean all

#  Set default /usr/bin/python to python3
RUN ln -s /usr/bin/python3 /usr/bin/python

# Install catch by hand for now:
COPY scripts/fetch-and-build-catch.sh /fetch-and-build-catch.sh
RUN /fetch-and-build-catch.sh


ENV LANG=C.UTF-8
RUN printf "LANG=C.UTF-8" > /etc/locale.conf

COPY src/test/docker/el9/config.site /usr/share/config.site

# the psm3 connector added to libfabrics in ~1.12 causes errors when allowed to
# use non-local connectors on a system with virtual NICs, since we're in a
# docker container, prevent this
ENV PSM3_HAL=loopback
# hwloc tries to look for opengl devices  by connecting to a port that might
# sometimes be an x11 port, but more often for us is munge, turn it off
ENV HWLOC_COMPONENTS=-gl
