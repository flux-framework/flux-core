#!/usr/bin/env bash
set -euo pipefail

mkdir catch2
pushd catch2
wget -O - https://github.com/catchorg/Catch2/archive/refs/tags/v3.6.0.tar.gz | tar xvz --strip-components 1
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j 4 -t install
popd
rm -rf catch2
