#!/bin/bash

die() {
    echo "$(basename $0): $@" >&2
    exit 1
}

test -f scripts/requirements-dev.txt || die "Please run from top of source tree"

eval "$(/opt/homebrew/bin/brew shellenv)"

brew install \
  autoconf \
  automake \
  libtool \
  make \
  pkg-config \
  epoll-shim \
  libev \
  zeromq \
  jansson \
  lz4 \
  libarchive \
  hwloc \
  sqlite \
  lua \
  python3 \
  cffi \
  libyaml \
  jq

python3 -m venv macos-venv
source macos-venv/bin/activate

pip3 install setuptools
pip3 install -r scripts/requirements-dev.txt

echo "Now run scripts/configure-macos.sh"
