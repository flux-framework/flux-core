#!/bin/bash

die() {
    echo "$(basename $0): $@" >&2
    exit 1
}

DEPS_SCRIPT=scripts/install-deps-macos.sh

test -f $DEPS_SCRIPT || die "please run from the top level of the source tree"
test -d macos-venv || die "please run $DEPS_SCRIPT first"

eval "$(/opt/homebrew/bin/brew shellenv)"

CPPFLAGS="-I${HOMEBREW_PREFIX}/include/lua"
CPPFLAGS="-I$(brew --prefix libev)/include ${CPPFLAGS}"
CPPFLAGS="-I$(brew --prefix epoll-shim)/include/libepoll-shim ${CPPFLAGS}"
LDFLAGS=-L${HOMEBREW_PREFIX}/lib

PKG_CONFIG_PATH=$(pkg-config --variable pc_path pkg-config)
PKG_CONFIG_PATH=$(brew --prefix libarchive)/lib/pkgconfig:${PKG_CONFIG_PATH}

PATH=$(brew --prefix libtool)/libexec/gnubin:$PATH

source macos-venv/bin/activate

./autogen.sh

CPPFLAGS="$CPPFLAGS" LDFLAGS=$LDFLAGS PKG_CONFIG_PATH=$PKG_CONFIG_PATH \
  ./configure --with-external-libev
