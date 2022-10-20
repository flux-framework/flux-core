#!/usr/bin/env bash
set -euxo pipefail
apt-get update
packages=(
  automake
  libsodium-dev
  libzmq3-dev
  libczmq-dev
  libjansson-dev
  libmunge-dev
  libncursesw5-dev
  lua5.1
  liblua5.1-dev
  liblz4-dev
  libsqlite3-dev
  uuid-dev
  libhwloc-dev
  libmpich-dev
  libs3-dev
  libevent-dev
  libarchive-dev
  python3
  python3-dev
  python3-pip
  python3-sphinx
  python3-venv
  libtool
  git
  build-essential
  # Flux security
  libjson-glib-1.0.0
  libjson-glib-dev
  libpam-dev
)
apt-get -qq install -y --no-install-recommends "${packages[@]}"
ldconfig
rm -rf /var/lib/apt/lists/*
