#!/usr/bin/env bash
set -euo pipefail

mkdir catch2
pushd catch2
wget -O - https://www.mpich.org/static/downloads/4.2.2/mpich-4.2.2.tar.gz | tar xvz --strip-components 1
mkdir -p build
pushd build
../configure --prefix=/usr --without-pmix
make -j 4 install
popd
popd
rm -rf catch2

