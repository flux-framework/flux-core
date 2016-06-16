#!/bin/sh
cppcheck --force --inline-suppr -j 2 --std=c99 --quiet \
    --error-exitcode=1 \
    -i src/common/libev \
    -i src/common/libsophia/ \
    -i src/common/liblsd \
    -i src/common/libtap \
    -i src/bindings/python \
    -i src/common/libutil/sds.c \
    src
