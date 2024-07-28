#!/usr/bin/env bash
set -euo pipefail

mkdir caliper
pushd caliper
wget -O - https://github.com/LLNL/Caliper/archive/v1.7.0.tar.gz | tar xvz --strip-components 1
# patch for uint32_t error
sed -i '39i#include <stdint.h>' include/caliper/common/Record.h
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr -DWITH_GOTCHA=Off
cmake --build build -j 4 -t install
popd
rm -rf caliper
