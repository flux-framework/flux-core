##
# Prologue
##
AC_INIT(m4_esyscmd([awk '/Name:/ {printf "%s",$2; exit}' META]),
        m4_esyscmd([awk '/Version:/ {printf "%s",$2; exit}' META]))
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_SRCDIR([NEWS])
AC_CANONICAL_SYSTEM
X_AC_META
X_AC_EXPAND_INSTALL_DIRS

##
# Automake support
##
AM_INIT_AUTOMAKE([subdir-objects])
AM_SILENT_RULES([yes])
AM_CONFIG_HEADER([config/config.h])
AM_MAINTAINER_MODE

AC_DEFINE([_GNU_SOURCE], 1,
          [Define _GNU_SOURCE so that we get all necessary prototypes])

##
# Checks for programs
##
AC_PROG_CC
AM_PROG_CC_C_O
if test "$GCC" = yes; then
  GCCWARN="-Wall -Werror -Wno-strict-aliasing"
  AC_SUBST([GCCWARN])
fi
AC_PROG_RANLIB
AC_PROG_LIBTOOL
AC_PROG_AWK
AC_CHECK_PROG(A2X,[a2x],[a2x])
if test "$A2X" != "a2x"; then
  AC_MSG_WARN([No a2x utility found. Manual pages will not be generated.])
fi
AM_CONDITIONAL([HAVE_A2X], [test "$A2X" = "a2x" ])
AC_CHECK_PROG(ASPELL,[aspell],[aspell])

##
# Checks for header files.
##
AC_HEADER_STDC
AC_CHECK_HEADERS( \
  pthread.h \
  getopt.h \
)

##
# Checks for typedefs, structures, and compiler characteristics
## 
AC_C_BIGENDIAN
AC_C_CONST

##
# Checks for library functions
##
AC_CHECK_FUNCS( \
  getopt_long \
  vsnprintf \
  vsscanf \
)
X_AC_CHECK_PTHREADS
X_AC_CHECK_COND_LIB(util, forkpty)
X_AC_CHECK_COND_LIB(rt, clock_gettime)
X_AC_CHECK_COND_LIB(dl, dlerror)
AX_PROG_LUA([5.1],[5.3])
AX_LUA_HEADERS
AX_LUA_LIBS
X_AC_ZEROMQ
X_AC_MUNGE
PKG_CHECK_MODULES(JSON, json, [],
  [PKG_CHECK_MODULES(JSON, json-c)]);
PKG_CHECK_MODULES(UUID, uuid)
LX_FIND_MPI
AM_CONDITIONAL([HAVE_MPI], [test "$have_C_mpi" = yes])

##
# For list.c, hostlist.c, hash.c
##
AC_DEFINE(WITH_PTHREADS, 1, [Make liblsd thread safe])

##
# Embedded libev
##
m4_include([src/common/libev/libev.m4])

AC_PKGCONFIG

##
# Project directories
##
AS_VAR_SET(fluxlibexecdir, $libexecdir/flux)
AC_SUBST(fluxlibexecdir)

AS_VAR_SET(fluxcmddir, $libexecdir/flux/core)
AC_SUBST(fluxcmddir)

AS_VAR_SET(fluxlibdir, $libdir/flux)
AC_SUBST(fluxlibdir)

AS_VAR_SET(fluxmoddir, $libdir/flux/modules)
AC_SUBST(fluxmoddir)

AS_VAR_SET(fluxconnectordir, $libdir/flux/connectors)
AC_SUBST(fluxconnectordir)

AS_VAR_SET(fluxincludedir, $includedir/flux)
AC_SUBST(fluxincludedir)

AS_VAR_SET(fluxcoreincludedir, $includedir/flux/core)
AC_SUBST(fluxcoreincludedir)

##
# Epilogue
##
AC_CONFIG_FILES( \
  Makefile \
  flux-core.spec \
  src/Makefile \
  src/common/Makefile \
  src/common/libtap/Makefile \
  src/common/liblsd/Makefile \
  src/common/libutil/Makefile \
  src/common/libev/Makefile \
  src/common/libzio/Makefile \
  src/common/libflux/Makefile \
  src/common/libmrpc/Makefile \
  src/lib/Makefile \
  src/lib/libcore/Makefile \
  src/lib/libpmi/Makefile \
  src/bindings/Makefile \
  src/bindings/lua/Makefile \
  src/broker/Makefile \
  src/cmd/Makefile \
  src/connectors/Makefile \
  src/connectors/local/Makefile \
  src/modules/Makefile \
  src/modules/api/Makefile \
  src/modules/kvs/Makefile \
  src/modules/modctl/Makefile \
  src/modules/live/Makefile \
  src/modules/mecho/Makefile \
  src/modules/barrier/Makefile \
  src/modules/wreck/Makefile \
  src/test/Makefile \
  src/test/kap/Makefile \
  src/test/request/Makefile \
  src/test/module/Makefile \
  etc/Makefile \
  etc/flux-core.pc \
  etc/Makefile.inc \
  etc/flux-libs.conf \
  doc/Makefile \
  doc/cmd/Makefile \
  t/Makefile \
  t/fluxometer.lua \
)
AC_CONFIG_FILES([src/cmd/flux-screen], [chmod +x src/cmd/flux-screen])

AC_OUTPUT
