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
  lua@5.4 \
  luarocks \
  python3 \
  cffi \
  libyaml \
  jq

# Make lua@5.4 the default lua, otherwise it will be 5.5:
brew link --force lua@5.4

# Point luarocks at the 5.4 installation
luarocks config lua_dir $(brew --prefix lua@5.4)
luarocks config lua_version 5.4

python3 -m venv macos-venv
source macos-venv/bin/activate

pip3 install setuptools
pip3 install -r scripts/requirements-dev.txt

luarocks --lua-version 5.4 install luaposix --tree /opt/homebrew

echo "Now run scripts/configure-macos.sh"
