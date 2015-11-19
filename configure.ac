##
# Prologue
##
AC_INIT(m4_esyscmd([awk '/Name:/ {printf "%s",$2; exit}' META]),
        m4_esyscmd([awk '/Version:/ {printf "%s",$2; exit}' META]))
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_MACRO_DIR([config])
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
AC_PROG_CC_C99
AM_PROG_CC_C_O
if test "$GCC" = yes; then
  GCCWARN="-Wall -Werror -Wno-strict-aliasing -Wno-error=deprecated-declarations"
  AC_SUBST([GCCWARN])
fi
LT_INIT
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

AC_MSG_CHECKING(--enable-python argument)
AC_ARG_ENABLE(python,
	[  --enable-python[=OPTS]   Include Python bindings. [default=yes] [OPTS=no/yes]], ,
	[enable_python="yes"])

AC_MSG_RESULT($enable_python)

if test "$enable_python" = "yes"; then
  AM_PATH_PYTHON([2.7])
  if test "X$PYTHON" != "X"; then
    AX_PYTHON_DEVEL
    #TODO: write this manually to add a version check
    AM_CHECK_PYMOD(cffi,
                   [cffi.__version_info__ >= (1,1)],
                   ,
                   [AC_MSG_ERROR([could not find python module cffi, version 1.1+ required])]
                   )
    python_ok=yes
  fi
  AS_VAR_SET(fluxpydir, $pyexecdir/flux)
  AC_SUBST(fluxpydir)
  AS_VAR_SET(fluxpymoddir, $pyexecdir/flux/modules)
  AC_SUBST(fluxpymoddir)

  if test "$python_ok" != "yes"; then
    AC_MSG_ERROR([could not configure python])
  fi
fi
AM_CONDITIONAL([HAVE_PYTHON], [test "$enable_python" = yes])

AX_PROG_LUA([5.1],[5.3])
AX_LUA_HEADERS
AX_LUA_LIBS
X_AC_ZEROMQ
X_AC_MUNGE
PKG_CHECK_MODULES([JSON], [json], [],
  [PKG_CHECK_MODULES([JSON], [json-c])])
PKG_CHECK_MODULES([HWLOC], [hwloc >= 1.4], [], [])
PKG_CHECK_MODULES([SQLITE], [sqlite3], [], [])
LX_FIND_MPI
AM_CONDITIONAL([HAVE_MPI], [test "$have_C_mpi" = yes])
AX_CODE_COVERAGE

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
# Macros to avoid repetition in Makefiles.am's
##
fluxmod_ldflags="-Wl,--no-undefined -avoid-version -export-symbols-regex '^mod_(main|name|service)\$\$' --disable-static -shared -export-dynamic"
AC_SUBST(fluxmod_ldflags)

fluxlib_ldflags="-shared -export-dynamic --disable-static -Wl,--no-undefined"
AC_SUBST(fluxlib_ldflags)

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
  src/common/libsophia/Makefile \
  src/common/libpmi-client/Makefile \
  src/common/libpmi-server/Makefile \
  src/common/libflux/Makefile \
  src/common/libsubprocess/Makefile \
  src/common/libcompat/Makefile \
  src/lib/Makefile \
  src/lib/libpmi/Makefile \
  src/bindings/Makefile \
  src/bindings/lua/Makefile \
  src/bindings/python/Makefile \
  src/bindings/python/flux/Makefile \
  src/bindings/python/flux/core/Makefile \
  src/broker/Makefile \
  src/cmd/Makefile \
  src/connectors/Makefile \
  src/connectors/local/Makefile \
  src/connectors/shmem/Makefile \
  src/connectors/loop/Makefile \
  src/modules/Makefile \
  src/modules/connector-local/Makefile \
  src/modules/kvs/Makefile \
  src/modules/content-sophia/Makefile \
  src/modules/modctl/Makefile \
  src/modules/libmrpc/Makefile \
  src/modules/libkz/Makefile \
  src/modules/libjsc/Makefile \
  src/modules/live/Makefile \
  src/modules/mecho/Makefile \
  src/modules/barrier/Makefile \
  src/modules/wreck/Makefile \
  src/modules/pymod/Makefile \
  src/modules/resource-hwloc/Makefile \
  src/test/Makefile \
  src/test/kap/Makefile \
  src/test/request/Makefile \
  src/test/module/Makefile \
  etc/Makefile \
  etc/flux-core.pc \
  etc/Makefile.inc \
  etc/flux-libs.conf \
  doc/Makefile \
  doc/man1/Makefile \
  doc/man3/Makefile \
  doc/test/Makefile \
  t/Makefile \
  t/fluxometer.lua \
)

AC_OUTPUT
