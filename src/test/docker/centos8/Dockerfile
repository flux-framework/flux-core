FROM centos:8

LABEL maintainer="Mark Grondona <mgrondona@llnl.gov>"

#  Enable PowerTools for development packages
RUN yum -y update \
 && dnf -y install 'dnf-command(config-manager)' \
 && yum config-manager --set-enabled PowerTools \
 && yum -y update \
#  Enable EPEL
 && yum -y install epel-release \
#  Utilities
 && yum -y install \
	wget \
	man-db \
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
#  Compilers, autotools
	pkgconfig \
	libtool \
	autoconf \
	automake \
	gcc \
	gcc-c++ \
	make \
	cmake \
#  Python
	python36 \
	python3-devel \
	python3-cffi \
	python3-six \
	python3-yaml \
	python3-jsonschema \
#  Development dependencies
	libsodium-devel \
	zeromq-devel \
	czmq-devel \
	jansson-devel \
	munge-devel \
	lz4-devel \
	sqlite-devel \
	libuuid-devel \
	hwloc-devel \
	mpich-devel \
	lua-devel \
	valgrind-devel \
	libs3-devel \
#  Other deps
	perl-Time-HiRes \
	lua-posix \
	libfaketime \
	cppcheck \
	enchant \
	aspell \
	aspell-en \
 && yum clean all

#  Set default /usr/bin/python to python3
RUN alternatives --set python /usr/bin/python3

#  Add /usr/bin/mpicc link so MPI tests are built
RUN alternatives --install /usr/bin/mpicc mpicc /usr/lib64/mpich/bin/mpicc 100

# Sphinx packages for docs
RUN python3 -m pip install sphinx sphinx-rtd-theme sphinxcontrib-spelling

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

ENV LANG=C.UTF-8

COPY config.site /usr/share/config.site
