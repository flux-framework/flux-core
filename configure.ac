##
# Prologue
##
AC_INIT([flux-core],
        m4_esyscmd([git describe --always | awk '/.*/ {sub(/^v/, ""); printf "%s",$1; exit}']))
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_MACRO_DIR([config])
AC_CONFIG_SRCDIR([NEWS])
AC_CANONICAL_SYSTEM
##
# If runstatedir not explicitly set on command line, use '/run' as default
# N.B. runstatedir is not set at all in autoconf < 2.70.
##
if test "$runstatedir" = '${localstatedir}/run' || test -z "$runstatedir"; then
   AC_SUBST([runstatedir],[/run])
fi
X_AC_EXPAND_INSTALL_DIRS

##
# Automake support
##
AM_INIT_AUTOMAKE([subdir-objects tar-pax])
AM_SILENT_RULES([yes])
AM_CONFIG_HEADER([config/config.h])
AM_MAINTAINER_MODE([enable])

AC_DEFINE([_GNU_SOURCE], 1,
          [Define _GNU_SOURCE so that we get all necessary prototypes])

##
# Generate project versions from PACKAGE_VERSION (set from git describe above)
##
AX_SPLIT_VERSION
AX_POINT_VERSION=$(echo $AX_POINT_VERSION | $SED 's/-.*$//')
AC_SUBST([AX_MAJOR_VERSION])
AC_SUBST([AX_MINOR_VERSION])
AC_SUBST([AX_POINT_VERSION])

##
# Initialize pkg-config for PKG_CHECK_MODULES to avoid conditional issues
##
PKG_PROG_PKG_CONFIG

##
# Library versions
##

# Revision rules
# https://www.gnu.org/software/libtool/manual/html_node/Updating-version-info.html

# 1) Start with version information 0:0:0 for each libtool library.
# 2) Update the version information only immediately before a public
#    release of your software. More frequent updates are unnecessary, and
#    only guarantee that the current interface number gets larger faster.
# 3) If the library source code has changed at all since the last
#    update, then increment revision ('c:r:a' becomes 'c:r+1:a').
# 4) If any interfaces have been added, removed, or changed since the
#    last update, increment current, and set revision to 0.
# 5) If any interfaces have been added since the last public release,
#    then increment age.
# 6) If any interfaces have been removed or changed since the last
#    public release, then set age to 0.

LIBFLUX_CORE_CURRENT=1
LIBFLUX_CORE_REVISION=0
LIBFLUX_CORE_AGE=0
LIBFLUX_CORE_VERSION_INFO=$LIBFLUX_CORE_CURRENT:$LIBFLUX_CORE_REVISION:$LIBFLUX_CORE_AGE
AC_SUBST([LIBFLUX_CORE_VERSION_INFO])

LIBFLUX_IDSET_CURRENT=1
LIBFLUX_IDSET_REVISION=0
LIBFLUX_IDSET_AGE=0
LIBFLUX_IDSET_VERSION_INFO=$LIBFLUX_IDSET_CURRENT:$LIBFLUX_IDSET_REVISION:$LIBFLUX_IDSET_AGE
AC_SUBST([LIBFLUX_IDSET_VERSION_INFO])

LIBFLUX_OPTPARSE_CURRENT=1
LIBFLUX_OPTPARSE_REVISION=0
LIBFLUX_OPTPARSE_AGE=0
LIBFLUX_OPTPARSE_VERSION_INFO=$LIBFLUX_OPTPARSE_CURRENT:$LIBFLUX_OPTPARSE_REVISION:$LIBFLUX_OPTPARSE_AGE
AC_SUBST([LIBFLUX_OPTPARSE_VERSION_INFO])

##
# Checks for programs
##
AC_PROG_CC_C99
AM_PROG_CC_C_O
AX_COMPILER_VENDOR
AS_CASE($ax_cv_c_compiler_vendor,
  [clang | gnu], [
    WARNING_CFLAGS="-Wall -Werror -Werror=missing-field-initializers -Wno-strict-aliasing -Wno-error=deprecated-declarations"
    AC_SUBST([WARNING_CFLAGS])
  ]
)

X_AC_ENABLE_SANITIZER
AC_COMPILE_IFELSE([AC_LANG_PROGRAM(,[
  #include <dlfcn.h>
  #if !(defined(RTLD_DEEPBIND))
  #error nope
  #endif
])], [has_deepbind=yes], [has_deepbind=no])
if test "x$san_enabled" != "xno" || test "x$has_deepbind" = "xno" ; then
  AC_DEFINE([FLUX_DEEPBIND], [0],
            [deepbind is unsupported with asan, musl and so-forth])
else
  AC_DEFINE([FLUX_DEEPBIND], [RTLD_DEEPBIND],
            [deepbind is unsupported with asan, musl and so-forth])
fi


LT_INIT
AC_PROG_AWK

AC_ARG_ENABLE([docs],
	      AS_HELP_STRING([--disable-docs], [disable building docs]))
AS_IF([test "x$enable_docs" != "xno"], [
  AC_CHECK_PROGS(ADOC, [a2x asciidoctor])
  AS_IF([test "$ADOC" == "a2x"], [
	ADOC_FORMAT_OPT="--format"
	AC_SUBST([ADOC_FORMAT_OPT])
  ])
  AS_IF([test "$ADOC" == "asciidoctor"], [
	ASCIIDOCTOR_VERSION=$(asciidoctor --version | sed -n 's/^Asciidoctor \([^ ]*\) .*$/\1/p')
	AX_COMPARE_VERSION([$ASCIIDOCTOR_VERSION],[ge],[1.5.7], [
	  ADOC_FORMAT_OPT="--backend"
	  AC_SUBST([ADOC_FORMAT_OPT])
  	  ], [
	  ADOC_ERROR_MSG="asciidoctor version >= 1.5.7 required"
	  AC_MSG_WARN([$ADOC_ERROR_MSG])
	  ADOC=""])
  ])
])
AM_CONDITIONAL([ENABLE_DOCS], [test -n "$ADOC"])
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
AX_COMPILE_CHECK_SIZEOF(uintptr_t, [#include <stdint.h>])
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

AX_PYTHON_DEVEL([>='2.7'])
AM_PATH_PYTHON([$ac_python_version])
if test "X$PYTHON" = "X"; then
  AC_MSG_ERROR([could not find python])
fi
# Flag for PYTHON_LDFLAGS workaround below.
if test -n "$PYTHON_LDFLAGS"; then
  ac_python_ldflags_set_by_user=true
fi

AM_CHECK_PYMOD(cffi,
               [cffi.__version_info__ >= (1,1)],
               ,
               [AC_MSG_ERROR([could not find python module cffi, version 1.1+ required])]
               )
AM_CHECK_PYMOD(six,
               [StrictVersion(six.__version__) >= StrictVersion('1.9.0')],
               ,
               [AC_MSG_ERROR([could not find python module six, version 1.9.0+ required])]
               )
AM_CHECK_PYMOD(yaml,
               [StrictVersion(yaml.__version__) >= StrictVersion ('3.10.0')],
               ,
               [AC_MSG_ERROR([could not find python module yaml, version 3.10+ required])]
               )
AM_CHECK_PYMOD(jsonschema,
               [StrictVersion(jsonschema.__version__) >= StrictVersion ('2.3.0')],
               ,
               [AC_MSG_ERROR([could not find python module jsonschema, version 2.3.0+ required])]
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
AS_VAR_SET(fluxpydir, $pyexecdir/flux)
AC_SUBST(fluxpydir)
AS_VAR_SET(fluxsodir, $pyexecdir/_flux)
AC_SUBST(fluxsodir)
AS_VAR_SET(fluxpymoddir, $pyexecdir/flux/modules)
AC_SUBST(fluxpymoddir)
AC_SUBST(PYTHON_LIBRARY, lib${ac_python_library}.so)

AC_DEFINE_UNQUOTED([PYTHON_INTERPRETER], ["$PYTHON"], [The python interpreter flux is configured with])

AC_ARG_ENABLE([pylint],
  [AS_HELP_STRING([--enable-pylint],
    [Enable pylint checks of python bindings])],,
  [enable_pylint="no"]
)
AS_IF([test "x$enable_pylint" = "xyes"], [
  AC_CHECK_PROG(PYLINT,[pylint],[pylint])
  AS_IF([test "x$PYLINT" != "xpylint"], [AC_MSG_ERROR([No pylint found in PATH])])
  AM_CHECK_PYMOD(pylint,
                   [StrictVersion(pylint.__version__) >= StrictVersion('1.4.5')],
                   ,
                   [AC_MSG_ERROR([could not find python module pylint, version 1.4.5+ required])]
                 )
])
AM_CONDITIONAL([ENABLE_PYLINT], [test "x$PYLINT" = "xpylint"])

AX_PROG_LUA([5.1],[5.3])
AX_LUA_HEADERS
AX_LUA_LIBS
X_AC_ZEROMQ
X_AC_JANSSON
PKG_CHECK_MODULES([HWLOC], [hwloc >= 1.11.1], [], [])
PKG_CHECK_MODULES([LZ4], [liblz4], [], [])
PKG_CHECK_MODULES([SQLITE], [sqlite3], [], [])
PKG_CHECK_MODULES([LIBSODIUM], [libsodium >= 1.0.14], [], [])
LX_FIND_MPI
AM_CONDITIONAL([HAVE_MPI], [test "$have_C_mpi" = yes])
AX_VALGRIND_H
AX_CODE_COVERAGE
AS_IF([test x$enable_code_coverage = xyes], [
    AC_DEFINE([CODE_COVERAGE_ENABLED], [1], [code coverage support])])

AC_ARG_WITH([flux-security], AS_HELP_STRING([--with-flux-security],
             [Build with flux-security]))
AS_IF([test "x$with_flux_security" = "xyes"], [
    PKG_CHECK_MODULES([FLUX_SECURITY], [flux-security],
                      [flux_sec_incdir=`$PKG_CONFIG --variable=includedir flux-security`],
                      [flux_sec_incdir=;])
    AS_IF([test "x$flux_sec_incdir" = x],
          [AC_MSG_ERROR([couldn't find flux-security or include directory])])
    AC_CHECK_HEADERS([flux/security/version.h])
    AC_DEFINE([HAVE_FLUX_SECURITY], [1], [Define flux-security is available])
    AC_SUBST(FLUX_SECURITY_INCDIR, $flux_sec_incdir)
])
AM_CONDITIONAL([HAVE_FLUX_SECURITY], [test "x$with_flux_security" = "xyes"])

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
# Check for systemd
##
RRA_WITH_SYSTEMD_UNITDIR


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

AS_VAR_SET(fluxrc1dir, $sysconfdir/flux/rc1.d)
AC_SUBST(fluxrc1dir)

AS_VAR_SET(fluxrc3dir, $sysconfdir/flux/rc3.d)
AC_SUBST(fluxrc3dir)

AS_VAR_SET(fluxcfdir, $sysconfdir/flux/conf.d)
AC_SUBST(fluxcfdir)

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
  src/common/libev/Makefile \
  src/common/libpmi/Makefile \
  src/common/libflux/Makefile \
  src/common/libflux/version.h \
  src/common/libkvs/Makefile \
  src/common/libkz/Makefile \
  src/common/libjob/Makefile \
  src/common/libzio/Makefile \
  src/common/libsubprocess/Makefile \
  src/common/libcompat/Makefile \
  src/common/liboptparse/Makefile \
  src/common/libidset/Makefile \
  src/common/libtomlc99/Makefile \
  src/common/libaggregate/Makefile \
  src/bindings/Makefile \
  src/bindings/lua/Makefile \
  src/bindings/python/Makefile \
  src/bindings/python/flux/Makefile \
  src/bindings/python/flux/core/Makefile \
  src/bindings/python/_flux/Makefile \
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
  src/modules/kvs-watch/Makefile \
  src/modules/content-sqlite/Makefile \
  src/modules/barrier/Makefile \
  src/modules/cron/Makefile \
  src/modules/aggregator/Makefile \
  src/modules/pymod/Makefile \
  src/modules/userdb/Makefile \
  src/modules/job-ingest/Makefile \
  src/modules/job-manager/Makefile \
  src/test/Makefile \
  etc/Makefile \
  etc/flux-core.pc \
  etc/flux-pmi.pc \
  etc/flux-optparse.pc \
  etc/flux-idset.pc \
  etc/flux.service \
  doc/Makefile \
  doc/man1/Makefile \
  doc/man3/Makefile \
  doc/man7/Makefile \
  doc/test/Makefile \
  t/Makefile \
  t/fluxometer/conf.lua \
  t/fluxometer/conf.lua.installed \
)

AC_CONFIG_LINKS([ \
  t/fluxometer.lua:t/fluxometer.lua \
])

AC_OUTPUT

AS_IF([test "x$enable_docs" != "xno"], [
  if test -z "$ADOC"; then
    AC_MSG_WARN([${ADOC_ERROR_MSG:-No asciidoc formatter found}. Manual pages will not be generated.])
  fi
])
