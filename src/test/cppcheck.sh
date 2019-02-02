#!/bin/sh
cppcheck --force --inline-suppr -j 2 --std=c99 --quiet \
    --error-exitcode=1 \
    -i src/common/libev \
    -i src/common/liblsd \
    -i src/common/libtap \
    -i src/common/libminilzo \
    -i src/bindings/python \
    -i src/common/libutil/sds.c \
    -i src/modules/kvs/test \
    -i src/broker/test \
    -i src/common/libzio/test \
    -i src/common/libkz/test \
    -i src/common/libtomlc99/test \
    -i src/common/liboptparse/test \
    -i src/common/libminilzo/test \
    -i src/common/libjob/test \
    -i src/common/libpmi/test \
    -i src/common/libidset/test \
    -i src/common/libutil/test \
    -i src/common/libkvs/test \
    -i src/common/libflux/test \
    -i src/bindings/python/pycotap/test \
    -i src/bindings/python/test \
    src
