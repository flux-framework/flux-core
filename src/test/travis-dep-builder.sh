#!/bin/bash
#
#

prefix=$HOME/local

#
# Ordered list of software to download and install into $prefix.
#  NOTE: Code currently assumes .tar.gz suffix...
#
downloads="\
https://github.com/dun/munge/archive/munge-0.5.11.tar.gz \
https://download.libsodium.org/libsodium/releases/libsodium-1.0.0.tar.gz \
http://download.zeromq.org/zeromq-4.0.4.tar.gz \
http://download.zeromq.org/czmq-3.0.2.tar.gz \
https://s3.amazonaws.com/json-c_releases/releases/json-c-0.11.tar.gz"

declare -A extra_configure_opts=(\
["zeromq-4.0.4"]="--with-libsodium --with-libsodium-include-dir=\$prefix/include" \
)

#
#  Lua rocks files to download and install:
#
declare -A lua_rocks=(\
["posix"]="luaposix" \
)

declare -r prog=${0##*/}
declare -r long_opts="prefix:,cachedir:,verbose"
declare -r short_opts="vp:c:"
declare -r usage="\
\n
Usage: $prog [OPTIONS]\n\
Download and install to a local prefix (default=$prefix) dependencies\n\
for building flux-framework/flux-core\n\
\n\
Options:\n\
 -v, --verbose           Be verbose.\n\
 -c, --cachedir=DIR      Check for precompiled dependency cache in DIR\n\
 -e, --max-cache-age=N   Expire cache in N days from creation\n\
 -p, --prefix=DIR        Install software into prefix\n
"

die() { echo -e "$prog: $@"; exit 1; }
say() { echo -e "$prog: $@"; }
debug() { test "$verbose" = "t" && say "$@"; }

GETOPTS=`/usr/bin/getopt -u -o $short_opts -l $long_opts -n $prog -- $@`
if test $? != 0; then
    die "$usage"
fi

eval set -- "$GETOPTS"
while true; do
    case "$1" in
      -v|--verbose)          verbose=t;     shift   ;;
      -c|--cachedir)         cachedir="$2"; shift 2 ;;
      -e|--max-cache-age)    cacheage="$2"; shift 2 ;;
      -p|--prefix)           prefix="$2";   shift   ;;
      --)                    shift ; break;         ;;
      *)                     die "Invalid option '$1'\n$usage" ;;
    esac
done

check_cache ()
{
    test -n "$cachedir" || return 1
    local url=$1
    local cachefile="${cachedir}/${url}"
    test -f "${cachefile}" || return 1
    test -n "$cacheage"    || return 0

    local ctime=$(stat -c%Y ${cachefile})
    local maxage=$((${cacheage}*86400))
    test $ctime -gt $maxage && return 1
}

add_cache ()
{
    test -n "$cachedir" || return 0
    mkdir -p "${cachedir}" &&
    touch "${cachedir}/${1}"
}


luarocks help >/dev/null 2>&1 || die "Required command luarocks not installed"

# install rocks
eval `luarocks --local path`
for p in ${!lua_rocks[@]}; do
    if ! lua -l$p -e '' >/dev/null 2>&1; then
        luarocks --local install ${lua_rocks[$p]}
    else
        say "Using cached version of " ${lua_rocks[$p]}
    fi
done

# special case for lua-hostlist
if ! lua -lhostlist -e '' >/dev/null 2>&1; then
   git clone https://github.com/grondo/lua-hostlist &&
   ( cd lua-hostlist &&
     export LUA_VER=5.1
     make &&
     make install PREFIX=$HOME/local LIBDIR=$HOME/.luarocks/lib
   )
else
   say "Using cached version of lua-hostlist"
fi

# hack for 'make install' targets that force installation to
#  /lib/systemd if $prefix/lib/systemd/system doesn't exist
mkdir -p ${prefix}/lib/systemd/system

for pkg in $downloads; do
    name=$(basename ${pkg} .tar.gz)
    if check_cache "$name"; then
       say "Using cached version of ${name}"
       continue
    fi
    mkdir -p ${name}  || die "Failed to mkdir ${name}"
    (
      cd ${name} &&
      wget ${pkg} || die "Failed to download ${pkg}"
      tar --strip-components=1 -xf *.tar.gz || die "Failed to un-tar ${name}"
      ./configure --prefix=${prefix} \
                  --sysconfdir=${prefix}/etc \
                  ${extra_configure_opts[$name]} &&
      make &&
      make install
    ) || die "Failed to build and install $name"
    add_cache "$name"
done


