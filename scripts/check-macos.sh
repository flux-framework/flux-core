#!/bin/bash

# Check what should work so far in src/common on macos

die() {
    echo "$(basename $0): $@" >&2
    exit 1
}

CONF_SCRIPT=scripts/configure-macos.sh

test -f $CONF_SCRIPT || die "please run from the top level of the source tree"
test -f configure || die "please run $CONF_SCRIPT first"

COMMON_WORKING="libtap libtestutil libyuarel libpmi liblsd libutil libflux libfluxutil libkvs libjob liboptparse libidset libtomlc99 libschedutil libeventlog libioencode librouter libdebugged libcontent libjob libhostlist libczmqcontainers libccan libzmqutil libtaskmap libfilemap libsdexec libmissing"
COMMON_BROKEN="libsubprocess libterminus librlist"

make -j4 -C src/common check SUBDIRS="$COMMON_WORKING" || die "check failed"

cat >&2 <<-EOT
=============================================
* Well the unit tests that worked before on macos still work!
* However, please note that the macos port of flux-core is incomplete.
* Search for 'macos' at https://github.com/flux-framework/flux-core/issues
* for portability issues that still need to be resolved.
=============================================
EOT
