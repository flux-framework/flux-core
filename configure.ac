##
# Prologue
##
AC_INIT([flux-core],
        m4_esyscmd([git describe --always | awk '/.*/ {sub(/^v/, ""); printf "%s",$1; exit}']))
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_MACRO_DIR([config])
AC_CONFIG_SRCDIR([NEWS])
AC_CANONICAL_SYSTEM
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
# Initialize pkg-config for PKG_CHECK_MODULES to avoid conditional issues
##
PKG_PROG_PKG_CONFIG

##
# Checks for programs
##
AC_PROG_CC_C99
AM_PROG_CC_C_O
if test "$GCC" = yes; then
  GCCWARN="-Wall -Werror -Wno-strict-aliasing -Wno-error=deprecated-declarations"
  AC_SUBST([GCCWARN])
fi
X_AC_ENABLE_SANITIZER
LT_INIT
AC_PROG_AWK

AC_ARG_ENABLE([docs],
	      AS_HELP_STRING([--disable-docs], [disable building docs]))
AS_IF([test "x$enable_docs" != "xno"], [
  AC_CHECK_PROG(A2X,[a2x],[a2x])
])
AM_CONDITIONAL([HAVE_A2X], [test "$A2X" = "a2x" ])
AC_CHECK_PROG(ASPELL,[aspell],[aspell])

##
# Checks for header files.
##
AC_HEADER_STDC
AC_CHECK_HEADERS( \
  pthread.h \
  getopt.h \
  fcntl.h \
  limits.h \
  strings.h \
  syslog.h \
  unistd.h \
  [sys/cdefs.h] \
  [sys/param.h] \
  stdarg.h \
  locale.h \
  xlocale.h \
  endian.h \
  inttypes.h \
)

##
# Checks for typedefs, structures, and compiler characteristics
## 
AC_C_BIGENDIAN
AC_C_CONST
AC_TYPE_SIZE_T
AX_COMPILE_CHECK_SIZEOF(int)
AX_COMPILE_CHECK_SIZEOF(long)
AX_COMPILE_CHECK_SIZEOF(long long)
AX_COMPILE_CHECK_SIZEOF(size_t, [#include <stdint.h>])


##
# Checks for library functions
##
AC_CHECK_FUNCS( \
  getopt_long \
  vsnprintf \
  vsscanf \
  realloc \
  strcasecmp \
  strdup \
  strerror \
  snprintf \
  vsnprintf \
  vasprintf \
  open \
  vsyslog \
  strncasecmp \
  setlocale \
  uselocale \
)
X_AC_CHECK_PTHREADS
X_AC_CHECK_COND_LIB(util, forkpty)
X_AC_CHECK_COND_LIB(rt, clock_gettime)
X_AC_CHECK_COND_LIB(dl, dlerror)
X_AC_MALLOC
AC_CHECK_LIB(m, floor)

AC_MSG_CHECKING(--enable-python argument)
AC_ARG_ENABLE(python,
	[  --enable-python[=OPTS]   Include Python bindings. [default=yes] [OPTS=no/yes]], ,
	[enable_python="yes"])

AC_MSG_RESULT($enable_python)

if test "$enable_python" = "yes"; then
  AM_PATH_PYTHON([2.7])
  if test "X$PYTHON" != "X"; then
    # Flag for PYTHON_LDFLAGS workaround below.
    if test -n "$PYTHON_LDFLAGS"; then
      ac_python_ldflags_set_by_user=true
    fi

    AX_PYTHON_DEVEL
    #TODO: write this manually to add a version check
    AM_CHECK_PYMOD(cffi,
                   [cffi.__version_info__ >= (1,1)],
                   ,
                   [AC_MSG_ERROR([could not find python module cffi, version 1.1+ required])]
                   )

    # Remove -L<path> from PYTHON_LDFLAGS if it is in a standard path
    # (e.g. /usr/lib64).  Placing a standard path earlier in the linker
    # search can lead to linking problems.
    # 
    # Logic below assumes only newer Python versions, protected by
    # above check for atleast Python 2.7.
    if test "$ac_python_ldflags_set_by_user" != "true"; then
      AC_CHECK_LIB([$ac_python_library], [PyArg_ParseTuple],
                   [ac_python_in_ld_path=true])
      if test "$ac_python_in_ld_path" = "true"; then
        AC_MSG_NOTICE([Removing -L$ac_python_libdir from PYTHON_LDFLAGS])
        PYTHON_LDFLAGS="-l$ac_python_library"
      fi
    fi
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
PKG_CHECK_MODULES([JANSSON], [jansson >= 2.6], [], [])
PKG_CHECK_MODULES([HWLOC], [hwloc >= 1.4], [], [])
PKG_CHECK_MODULES([SQLITE], [sqlite3], [], [])
LX_FIND_MPI
AM_CONDITIONAL([HAVE_MPI], [test "$have_C_mpi" = yes])
AX_CODE_COVERAGE

AC_ARG_ENABLE(caliper,
	[  --enable-caliper[=OPTS]   Use caliper for profiling. [default=no] [OPTS=no/yes]], ,
	[enable_caliper="no"])

if test "$enable_caliper" = "yes"; then
    PKG_CHECK_MODULES([CALIPER], [caliper], [], [])
    CFLAGS="${CFLAGS} ${CALIPER_CFLAGS} "
    # Do not use CALIPER_LIBS, only link to libcaliper-stub
    LIBS="${LIBS} $(pkg-config --libs-only-L caliper) -lcaliper-stub -lrt "
    AC_DEFINE([HAVE_CALIPER], [1], [Define if you have libcaliper])
fi


##
# Embedded libev
##
m4_include([src/common/libev/libev.m4])

AC_PKGCONFIG

##
# Project directories
##
AS_VAR_SET(fluxrcdir, $sysconfdir/flux)
AC_SUBST(fluxrcdir)

AS_VAR_SET(fluxlibexecdir, $libexecdir/flux)
AC_SUBST(fluxlibexecdir)

AS_VAR_SET(fluxcmddir, $libexecdir/flux/cmd)
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

adl_RECURSIVE_EVAL([$bindir], fluxbindir)
AS_VAR_SET(fluxbindir, $fluxbindir)
AC_SUBST(fluxbindir)

adl_RECURSIVE_EVAL([$luadir], fluxluadir)
AS_VAR_SET(fluxluadir, $fluxluadir)
AC_SUBST(fluxluadir)

##
# Macros to avoid repetition in Makefiles.am's
##
fluxmod_ldflags="$san_ld_zdef_flag -avoid-version -export-symbols-regex '^mod_(main|name|service)\$\$' --disable-static -shared -export-dynamic"
AC_SUBST(fluxmod_ldflags)

fluxlib_ldflags="-shared -export-dynamic --disable-static $san_ld_zdef_flag"
AC_SUBST(fluxlib_ldflags)

##
# Epilogue
##
AC_CONFIG_FILES( \
  Makefile \
  src/Makefile \
  src/common/Makefile \
  src/common/libtap/Makefile \
  src/common/liblsd/Makefile \
  src/common/libutil/Makefile \
  src/common/libjson-c/Makefile \
  src/common/libev/Makefile \
  src/common/libminilzo/Makefile \
  src/common/libpmi/Makefile \
  src/common/libflux/Makefile \
  src/common/libsubprocess/Makefile \
  src/common/libcompat/Makefile \
  src/common/liboptparse/Makefile \
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
  src/connectors/ssh/Makefile \
  src/modules/Makefile \
  src/modules/connector-local/Makefile \
  src/modules/kvs/Makefile \
  src/modules/content-sqlite/Makefile \
  src/modules/libkz/Makefile \
  src/modules/libjsc/Makefile \
  src/modules/live/Makefile \
  src/modules/barrier/Makefile \
  src/modules/wreck/Makefile \
  src/modules/resource-hwloc/Makefile \
  src/modules/cron/Makefile \
  src/modules/aggregator/Makefile \
  src/modules/pymod/Makefile \
  src/test/Makefile \
  src/test/kap/Makefile \
  etc/Makefile \
  etc/flux-core.pc \
  doc/Makefile \
  doc/man1/Makefile \
  doc/man3/Makefile \
  doc/man7/Makefile \
  doc/test/Makefile \
  t/Makefile \
  t/fluxometer/conf.lua \
  t/fluxometer/conf.lua.installed \
)

AC_CONFIG_FILES(
  [t/t9990-python-tests.t:t/t9990-python-tests.t.in],
  [chmod +x t/t9990-python-tests.t]
                )

AC_CONFIG_LINKS([ \
  t/fluxometer.lua:t/fluxometer.lua \
])

AC_OUTPUT

AS_IF([test "x$enable_docs" != "xno"], [
  if test "$A2X" != "a2x"; then
    AC_MSG_WARN([No asciidoc formatter found. Manual pages will not be generated.])
  fi
])
