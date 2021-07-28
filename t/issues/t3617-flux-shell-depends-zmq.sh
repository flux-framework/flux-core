#!/bin/sh -e
# flux-shell does not link to libzmq

FLUX_SHELL="${FLUX_BUILD_DIR}/src/shell/.libs/lt-flux-shell"
if libtool --mode=execute ldd ${FLUX_SHELL} | grep -q zmq
then
    exit 1
fi
exit 0
