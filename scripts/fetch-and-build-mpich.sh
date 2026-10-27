#!/usr/bin/env bash
set -euo pipefail

# Work around an MPICH 5.0.x configure bug: AC_CHECK_FUNCS([PMI_Barrier_group])
# runs only when linking a third party PMI library, so with MPICH's own
# embedded libpmi the HAVE_PMI_BARRIER_GROUP guard in src/util/mpir_pmi1.inc
# is never defined and MPICH silently compiles out its PMI-1.2 group barrier.
# There is no configure option for this.  Define the macro via CPPFLAGS so
# the image's MPICH behaves as released.  Drop this once MPICH probes for
# the function unconditionally.
MPICH_VERSION=5.0.2
MPICH_CPPFLAGS="-DHAVE_PMI_BARRIER_GROUP=1"

mkdir mpich
pushd mpich
wget -O - https://www.mpich.org/static/downloads/${MPICH_VERSION}/mpich-${MPICH_VERSION}.tar.gz | tar xvz --strip-components 1
mkdir -p build
pushd build
../configure --prefix=/usr --without-pmix --disable-fortran \
    CPPFLAGS="${MPICH_CPPFLAGS}"
make -j 4 install
popd
popd
rm -rf mpich

