##
# Prologue
##
AC_INIT([flux-core],
        m4_esyscmd([test -n "$FLUX_VERSION" && printf $FLUX_VERSION || git describe --always | awk '/.*/ {sub(/^v/, ""); printf "%s",$1; exit}']))
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_MACRO_DIR([config])
AC_CONFIG_SRCDIR([NEWS.md])
AC_CANONICAL_TARGET
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
AM_INIT_AUTOMAKE([subdir-objects tar-pax foreign])
AM_SILENT_RULES([yes])
AC_CONFIG_HEADERS([config/config.h])
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

AC_MSG_CHECKING([whether version number is sane])
AS_IF([printf "%d.%d.%d" ${AX_MAJOR_VERSION} ${AX_MINOR_VERSION} ${AX_POINT_VERSION} >/dev/null 2>&1], [
    AC_MSG_RESULT([yes])
],[
    AC_MSG_RESULT([no])
    version_err_msg="
    VERSION ${VERSION} is invalid.
    Try the following to remedy this:

      1. Run \`git fetch --tags\` before building. Versions in
         flux-core are derived from \`git describe\` which uses
	 the most recent tag.
      2. If you are running remote CI in a fork of the main repository,
         try pushing the upstream tags to your fork with
	 \`git push --tags <your_remote>\` to make sure tags are
	 synchronized in your fork.
      3. Set the variable manually, with FLUX_VERSION=<version>
         in your environment.
    "
    AC_MSG_ERROR(["${version_err_msg}"])
])

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

LIBFLUX_CORE_CURRENT=2
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

LIBFLUX_SCHEDUTIL_CURRENT=1
LIBFLUX_SCHEDUTIL_REVISION=0
LIBFLUX_SCHEDUTIL_AGE=0
LIBFLUX_SCHEDUTIL_VERSION_INFO=$LIBFLUX_SCHEDUTIL_CURRENT:$LIBFLUX_SCHEDUTIL_REVISION:$LIBFLUX_SCHEDUTIL_AGE
AC_SUBST([LIBFLUX_SCHEDUTIL_VERSION_INFO])

LIBFLUX_HOSTLIST_CURRENT=1
LIBFLUX_HOSTLIST_REVISION=0
LIBFLUX_HOSTLIST_AGE=0
LIBFLUX_HOSTLIST_VERSION_INFO=$LIBFLUX_HOSTLIST_CURRENT:$LIBFLUX_HOSTLIST_REVISION:$LIBFLUX_HOSTLIST_AGE
AC_SUBST([LIBFLUX_HOSTLIST_VERSION_INFO])

LIBFLUX_TASKMAP_CURRENT=1
LIBFLUX_TASKMAP_REVISION=0
LIBFLUX_TASKMAP_AGE=0
LIBFLUX_TASKMAP_VERSION_INFO=$LIBFLUX_TASKMAP_CURRENT:$LIBFLUX_TASKMAP_REVISION:$LIBFLUX_TASKMAP_AGE
AC_SUBST([LIBFLUX_TASKMAP_VERSION_INFO])

##
# Checks for programs
##
m4_version_prereq(2.70, [AC_PROG_CC], [AC_PROG_CC_C99])
AM_PROG_CC_C_O
AX_COMPILER_VENDOR
AX_COMPILER_VERSION
AS_CASE($ax_cv_c_compiler_vendor,
  [clang | gnu], [
    WARNING_CFLAGS="-Wall -Werror -Wno-strict-aliasing -Wno-error=deprecated-declarations"
  ]
)

# Clang < 6.x fails to compile flux with -Werror=missing-field-initializers
#
AC_MSG_CHECKING([If -Werror=missing-field-initializers can be used])
cc_major=`echo $ax_cv_c_compiler_version | $SED 's/\([[^.]][[^.]]*\).*/\1/'`
AS_IF([test "x$ax_cv_c_compiler_vendor" = "xclang" -a $cc_major -lt 6], [
  AC_MSG_RESULT([no])
  ], [
  AC_MSG_RESULT([yes])
  WARNING_CFLAGS="$WARNING_CFLAGS -Werror=missing-field-initializers"
  ]
)
AC_SUBST([WARNING_CFLAGS])

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

# N.B. /usr/bin/rsh is a symlink to preferred remote shell on some systems
AC_ARG_VAR(SSH, [The path to preferred remote shell])
AC_PATH_PROGS(SSH, [rsh ssh], [/usr/bin/rsh])
AC_DEFINE_UNQUOTED([PATH_SSH], "$SSH",
          [Define remote shell program to be used by the ssh:// connector])

AC_MSG_CHECKING([whether Apple frameworks can be used])
AS_IF([test $(uname) = "Darwin"],[
  AC_MSG_RESULT([yes])
  apple_coreservices_framework="-framework CoreServices"
  AS_VAR_SET(APPLE_CORESERVICES_FRAMEWORK, $apple_coreservices_framework)
  AC_SUBST(APPLE_CORESERVICES_FRAMEWORK)
  apple_systemconfiguration_framework="-framework SystemConfiguration"
  AS_VAR_SET(APPLE_SYSTEMCONFIGURATION_FRAMEWORK, $apple_systemconfiguration_framework)
  AC_SUBST(APPLE_SYSTEMCONFIGURATION_FRAMEWORK)
],[
  AC_MSG_RESULT([no])
])

# macos linker doesn't support --gc-sections
saved_LDFLAGS="$LDFLAGS"
LDFLAGS="-Wl,--gc-sections $LDFLAGS"
AC_MSG_CHECKING([whether ld supports --gc-sections])
AC_LINK_IFELSE([AC_LANG_PROGRAM([])], [
  AC_MSG_RESULT([yes])
  ld_gc_sections="-Wl,--gc-sections"
  AS_VAR_SET(ld_gc_sections, $ld_gc_sections)
  AC_SUBST(ld_gc_sections)
  ],[
  AC_MSG_RESULT([no])
])
LDFLAGS=$saved_LDFLAGS

LT_INIT
AC_PROG_AWK

##
# Checks for header files.
##
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
  link.h \
  [sys/ucred.h] \
  [sys/prctl.h] \
  [sys/vfs.h] \
  [linux/magic.h] \
)

##
# Checks for typedefs, structures, and compiler characteristics
##
AC_C_BIGENDIAN(
  [AC_DEFINE([HAVE_BIG_ENDIAN], [1], [big endian])],
  [AC_DEFINE([HAVE_LITTLE_ENDIAN], [1], [little endian])]
)
AC_C_CONST
AC_TYPE_SIZE_T
AX_COMPILE_CHECK_SIZEOF(int)
AX_COMPILE_CHECK_SIZEOF(long)
AX_COMPILE_CHECK_SIZEOF(long long)
AX_COMPILE_CHECK_SIZEOF(uintptr_t, [#include <stdint.h>])
AX_COMPILE_CHECK_SIZEOF(ptrdiff_t, [#include <stddef.h>])
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
  inotify_init1 \
)
# See src/common/libmissing/Makefile.am
AC_REPLACE_FUNCS( \
  strlcpy \
  strlcat \
  argz_add \
  envz_add \
  strerrorname_np \
  pipe2 \
  mempcpy \
  get_current_dir_name \
)
X_AC_CHECK_PTHREADS
X_AC_CHECK_COND_LIB(rt, clock_gettime)
X_AC_CHECK_COND_LIB(dl, dlerror)
X_AC_MALLOC
AC_CHECK_LIB(m, floor)
AC_SEARCH_LIBS(epoll_create1, epoll-shim)
AM_CONDITIONAL([HAVE_INOTIFY], [test "x$ac_cv_func_inotify_init1" = xyes])

AC_MSG_CHECKING([for pthread_setname_np with tid parameter])
AC_COMPILE_IFELSE(
  [AC_LANG_PROGRAM(
    [#include <pthread.h>],
    [pthread_setname_np(pthread_self(), "example")])],
  [AC_MSG_RESULT(yes)
    AC_DEFINE(HAVE_PTHREAD_SETNAME_NP_WITH_TID, 1,
      [Have pthread_setname_np() with tid])],
  [AC_MSG_RESULT(no)]
)

AC_ARG_ENABLE([docs],
	      AS_HELP_STRING([--disable-docs], [disable building docs]))

#  Edit PATH to remove $PWD/src/cmd so that AM_PATH_PYTHON doesn't find
#  flux python script (thus creating a link to itself.) This needs to be
#  done *before* AX_PYTHON_DEVEL.
#
saved_PATH=$PATH
export PATH=$(echo $PATH | sed "s|$(pwd)/src/cmd:*||")

if test "X$PYTHON_VERSION" = "X" ; then
  if test "X$PYTHON" = "X" ; then
    # if the user hasn't specified, try for python 3
    PYTHON_VERSION=3
  fi
fi

# Do not let AX_PYTHON_DEVEL set PYTHON_SITE_PKG
saved_PYTHON_SITE_PKG=$PYTHON_SITE_PKG
AX_PYTHON_DEVEL([>='3.6'])
PYTHON_SITE_PKG=$saved_PYTHON_SITE_PKG

AM_PATH_PYTHON([$ac_python_version])
if test "X$PYTHON" = "X"; then
    AC_MSG_ERROR([could not find python])
fi
#  Restore original PATH:
export PATH=${saved_PATH}

# Flag for PYTHON_LDFLAGS workaround below.
if test -n "$PYTHON_LDFLAGS"; then
  ac_python_ldflags_set_by_user=true
fi

AM_CHECK_PYMOD(cffi,
               [cffi.__version_info__ >= (1,1)],
               ,
               [AC_MSG_ERROR([could not find python module cffi, version 1.1+ required])]
               )
AM_CHECK_PYMOD(yaml,
               [Version(yaml.__version__) >= Version ('3.10.0')],
               ,
               [AC_MSG_ERROR([could not find python module yaml, version 3.10+ required])]
               )
AM_CHECK_PYMOD(ply,
               [Version(ply.__version__) >= Version ('3.9')],
               ,
               [AC_MSG_ERROR([could not find python module ply, version 3.9+ required])]
               )

AS_IF([test "x$enable_docs" != "xno"], [
            AM_CHECK_PYMOD(sphinx,
                           [Version(sphinx.__version__) >= Version ('1.6.7')],
                           [sphinx=true],
                           [sphinx=false; AC_MSG_WARN([could not find sphinx to generate docs, version 1.6.7+ required])]
                           )
            AM_CHECK_PYMOD(docutils,
                           [Version(docutils.__version__) >= Version ('0.11.0')],
                           [docutils=true],
                           [docutils=false; AC_MSG_WARN([could not find docutils to generate docs, version 0.11.0+ required])]
                           )
])
#  If --enable-docs=yes, but no doc generator found,
#   then error immediately:
#
AS_IF([test "x$enable_docs" = "xyes" -a "x$sphinx" = "xfalse"],[
  AC_MSG_ERROR([--enable-docs used but no document generator found!])
])
AS_IF([test "x$enable_docs" = "xyes" -a "x$docutils" = "xfalse"],[
  AC_MSG_ERROR([--enable-docs used but docutils not found!])
])
AM_CONDITIONAL([ENABLE_DOCS], [test "x$sphinx" = "xtrue" -a "x$docutils" = "xtrue"])
AC_CHECK_PROG(ASPELL,[aspell],[aspell])


# Remove -L<path> from PYTHON_LDFLAGS if it is in a standard path
# (e.g. /usr/lib64).  Placing a standard path earlier in the linker
# search can lead to linking problems.
#
# Logic below assumes only newer Python versions, protected by
# above check for atleast Python 3.6.
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
AS_VAR_SET(fluxpysodir, $pyexecdir/_flux)
AC_SUBST(fluxpysodir)
AC_SUBST(PYTHON_LIBRARY, lib${ac_python_library}.so)

AC_DEFINE_UNQUOTED([PYTHON_INTERPRETER], ["$PYTHON"], [The python interpreter flux is configured with])
AC_SUBST(PYTHON)

AC_ARG_ENABLE([pylint],
  [AS_HELP_STRING([--enable-pylint],
    [Enable pylint checks of python bindings])],,
  [enable_pylint="no"]
)
AS_IF([test "x$enable_pylint" = "xyes"], [
  AC_CHECK_PROG(PYLINT,[pylint],[pylint])
  AS_IF([test "x$PYLINT" != "xpylint"], [AC_MSG_ERROR([No pylint found in PATH])])
  AM_CHECK_PYMOD(pylint,
                   [Version(pylint.__version__) >= Version('1.8.4')],
                   ,
                   [AC_MSG_ERROR([could not find python module pylint, version 1.8.4+ required])]
                 )
])
AM_CONDITIONAL([ENABLE_PYLINT], [test "x$PYLINT" = "xpylint"])

AX_PROG_LUA([5.1],[5.5])
AX_LUA_HEADERS
AX_LUA_LIBS
AC_MSG_CHECKING([for luaposix])
if $LUA -l posix -e ''; then
  AC_MSG_RESULT([yes])
else
  AC_MSG_RESULT([no])
  AC_MSG_FAILURE([Could not find required Lua posix module])
fi
X_AC_ZEROMQ
X_AC_JANSSON
PKG_CHECK_MODULES([LIBSYSTEMD], [libsystemd >= 0.23],
                                [have_libsystemd=yes], [have_libsystemd=no])
AC_CHECK_HEADER([systemd/sd-bus.h], [have_sd_bus_h=yes], [have_sd_bus_h=no])
AM_CONDITIONAL([HAVE_LIBSYSTEMD],
        [test "$have_libsystemd" = yes -a "$have_sd_bus_h" = yes])
if test "$have_libsystemd" = yes -a "$have_sd_bus_h" = yes; then
    AC_DEFINE([HAVE_LIBSYSTEMD], [1], [Define if you have libsystemd])
    AC_CHECK_LIB(systemd, sd_bus_message_dump,
                 AC_DEFINE([HAVE_SD_BUS_MESSAGE_DUMP], [1],
                           [Define if you have sd_bus_message_dump]))
fi
PKG_CHECK_MODULES([HWLOC], [hwloc >= 1.11.1], [], [])
PKG_CHECK_MODULES([LZ4], [liblz4], [], [])
PKG_CHECK_MODULES([SQLITE], [sqlite3], [], [])
PKG_CHECK_MODULES([LIBUUID], [uuid], [], [])
PKG_CHECK_MODULES([CURSES], [ncursesw], [], [])
PKG_CHECK_MODULES([LIBARCHIVE], [libarchive], [], [])
PKG_CHECK_MODULES([BASH_COMPLETION], [bash-completion], [
    bashcompdir="`$PKG_CONFIG \
        --define-variable=datadir=${datadir} \
        --variable=completionsdir bash-completion`"
], [
    bashcompdir="${datadir}/bash-completion/completions"
])
AC_SUBST(bashcompdir)

LX_FIND_MPI
AM_CONDITIONAL([HAVE_MPI], [test "$have_C_mpi" = yes])
AX_VALGRIND_H
AX_CODE_COVERAGE
AS_IF([test x$enable_code_coverage = xyes], [
    AC_DEFINE([CODE_COVERAGE_ENABLED], [1], [code coverage support])])

AC_ARG_WITH([flux-security], AS_HELP_STRING([--with-flux-security],
             [Build with flux-security]))
AS_IF([test "x$with_flux_security" = "xyes"], [
    PKG_CHECK_MODULES([FLUX_SECURITY], [flux-security >= 0.13.0],
      [flux_sec_incdir=`$PKG_CONFIG --variable=includedir flux-security`])
    AS_IF([test "x$flux_sec_incdir" = x],
          [AC_MSG_ERROR([couldn't find flux-security include directory])])
    AC_CHECK_HEADERS([flux/security/version.h])
    AC_DEFINE([HAVE_FLUX_SECURITY], [1], [Define flux-security is available])
    AC_SUBST(FLUX_SECURITY_INCDIR, $flux_sec_incdir)
])
AM_CONDITIONAL([HAVE_FLUX_SECURITY], [test "x$with_flux_security" = "xyes"])

AC_ARG_ENABLE(
    [broken-locale-mode],
    AS_HELP_STRING([--enable-broken-locale-mode],
                   [Assume broken locale config and use ascii only]))

if test "$enable_broken_locale_mode" = "yes"; then
  AC_DEFINE([ASSUME_BROKEN_LOCALE], [1],
    [assume broken locale configuration and disable non-ascii characters])
fi

##
# Check for systemd
##
RRA_WITH_SYSTEMD_UNITDIR


##
# libev
##
AC_ARG_WITH([external-libev],
  AS_HELP_STRING([--with-external-libev], [Use external libev]))
AS_IF([test "x$with_external_libev" = "xyes"], [
  AC_SEARCH_LIBS([ev_run], [ev], [], [
    AC_MSG_ERROR([--with-external-libev requested but external libev not found])
  ])
],[
  m4_include([src/common/libev/libev.m4])
])
AM_CONDITIONAL([INTERNAL_LIBEV],[test "x$with_external_libev" != "xyes"])

AC_PKGCONFIG

##
# Project directories
##
adl_RECURSIVE_EVAL(["$sysconfdir/flux"], [fluxconfdir])
AC_DEFINE_UNQUOTED([FLUXCONFDIR], ["$fluxconfdir"],
  [The expansion of "$sysconfdir/flux"])
AC_SUBST(fluxconfdir)

adl_RECURSIVE_EVAL(["$libexecdir/flux"], [fluxlibexecdir])
AC_DEFINE_UNQUOTED([FLUXLIBEXECDIR], ["$fluxlibexecdir"],
  [The expansion of "$libexecdir/flux"])
AC_SUBST(fluxlibexecdir)

adl_RECURSIVE_EVAL(["$datadir/flux"], [fluxdatadir])
AC_DEFINE_UNQUOTED([FLUXDATADIR], ["$fluxdatadir"],
  [The expansion of "$datadir/flux"])
AC_SUBST(fluxdatadir)

adl_RECURSIVE_EVAL(["$libexecdir/flux/cmd"], [fluxcmddir])
AC_DEFINE_UNQUOTED([FLUXCMDDIR], ["$fluxcmddir"],
  [The expansion of "$libexecdir/flux/cmd"])
AC_SUBST(fluxcmddir)

adl_RECURSIVE_EVAL(["$libdir/flux"], [fluxlibdir])
AC_DEFINE_UNQUOTED([FLUXLIBDIR], ["$fluxlibdir"],
  [The expansion of "$libdir/flux"])
AC_SUBST(fluxlibdir)

# Target of PYTHONPATH set by flux(1) cmddriver, so flux(1)
# doesn't inadvertently insert system python paths (or any
# other python path for that matter) first in PYTHONPATH.
#
adl_RECURSIVE_EVAL(["$fluxlibdir/python$PYTHON_VERSION"], [fluxpylinkdir])
AC_DEFINE_UNQUOTED([FLUXPYLINKDIR], ["$fluxpylinkdir"],
  [The expansion of "$fluxlibdir/python$PYTHON_VERSION"])
AC_SUBST(fluxpylinkdir)

adl_RECURSIVE_EVAL(["$libdir/flux/modules"], [fluxmoddir])
AC_DEFINE_UNQUOTED([FLUXMODDIR], ["$fluxmoddir"],
  [The expansion of "$libdir/flux/modules"])
AC_SUBST(fluxmoddir)

adl_RECURSIVE_EVAL(["$libdir/flux/job-manager/plugins"], [jobtap_plugindir])
AC_DEFINE_UNQUOTED([JOBTAP_PLUGINDIR], ["$jobtap_plugindir"],
  [The expansion of "$libdir/flux/job-manager/plugins"])
AC_SUBST(jobtap_plugindir)

adl_RECURSIVE_EVAL(["$libdir/flux/shell/plugins"], [shell_plugindir])
AC_DEFINE_UNQUOTED([SHELL_PLUGINDIR], ["$shell_plugindir"],
  [The expansion of "$libdir/flux/shell/plugins"])
AC_SUBST(shell_plugindir)

adl_RECURSIVE_EVAL(["$libdir/flux/connectors"], [fluxconnectordir])
AC_DEFINE_UNQUOTED([FLUXCONNECTORDIR], ["$fluxconnectordir"],
  [The expansion of "$libdir/flux/connectors"])
AC_SUBST(fluxconnectordir)

AS_VAR_SET(fluxincludedir, $includedir/flux)
AC_SUBST(fluxincludedir)

AS_VAR_SET(fluxcoreincludedir, $includedir/flux/core)
AC_SUBST(fluxcoreincludedir)

AS_VAR_SET(fluxschedutilincludedir, $includedir/flux/schedutil)
AC_SUBST(fluxschedutilincludedir)

adl_RECURSIVE_EVAL([$bindir], fluxbindir)
AS_VAR_SET(fluxbindir, $fluxbindir)
AC_SUBST(fluxbindir)

adl_RECURSIVE_EVAL([$luadir], fluxluadir)
AS_VAR_SET(fluxluadir, $fluxluadir)
AC_SUBST(fluxluadir)

AS_VAR_SET(fluxbindingincludedir, $includedir/flux/_binding)
AC_SUBST(fluxbindingincludedir)


##
# Macros to avoid repetition in Makefiles.am's
##
fluxmod_ldflags="$san_ld_zdef_flag -avoid-version -export-symbols-regex '^mod_(main|name)\$\$' --disable-static -shared -export-dynamic $ld_gc_sections"
AC_SUBST(fluxmod_ldflags)

fluxplugin_ldflags="-avoid-version -export-symbols-regex '^flux_plugin_init\$\$' --disable-static -shared -export-dynamic $ld_gc_sections"
AC_SUBST(fluxplugin_ldflags)

fluxlib_ldflags="-shared -export-dynamic --disable-static $san_ld_zdef_flag $ld_gc_sections"
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
  src/common/libfluxutil/Makefile \
  src/common/libtestutil/Makefile \
  src/common/libkvs/Makefile \
  src/common/libcontent/Makefile \
  src/common/libjob/Makefile \
  src/common/libsubprocess/Makefile \
  src/common/liboptparse/Makefile \
  src/common/libidset/Makefile \
  src/common/libtomlc99/Makefile \
  src/common/libschedutil/Makefile \
  src/common/libeventlog/Makefile \
  src/common/libioencode/Makefile \
  src/common/librouter/Makefile \
  src/common/libyuarel/Makefile \
  src/common/libdebugged/Makefile \
  src/common/libterminus/Makefile \
  src/common/libhostlist/Makefile \
  src/common/librlist/Makefile \
  src/common/libczmqcontainers/Makefile \
  src/common/libccan/Makefile \
  src/common/libzmqutil/Makefile \
  src/common/libtaskmap/Makefile \
  src/common/libfilemap/Makefile \
  src/common/libsdexec/Makefile \
  src/common/libsqlite3/Makefile \
  src/common/libmissing/Makefile \
  src/bindings/Makefile \
  src/bindings/lua/Makefile \
  src/bindings/python/Makefile \
  src/bindings/python/flux/Makefile \
  src/bindings/python/_flux/Makefile \
  src/broker/Makefile \
  src/cmd/Makefile \
  src/shell/Makefile \
  src/connectors/Makefile \
  src/modules/Makefile \
  src/modules/kvs/Makefile \
  src/modules/content-files/Makefile \
  src/modules/job-ingest/Makefile \
  src/modules/job-manager/Makefile \
  src/modules/job-list/Makefile \
  src/modules/job-exec/Makefile \
  src/modules/resource/Makefile \
  src/modules/sdbus/Makefile \
  src/test/Makefile \
  etc/Makefile \
  etc/flux-core.pc \
  etc/flux-pmi.pc \
  etc/flux-optparse.pc \
  etc/flux-idset.pc \
  etc/flux-schedutil.pc \
  etc/flux-hostlist.pc \
  etc/flux-taskmap.pc \
  etc/flux.service \
  etc/flux-housekeeping@.service \
  etc/flux-run-system-scripts \
  src/cmd/flux-run-housekeeping \
  etc/flux-prolog@.service \
  etc/flux-epilog@.service \
  src/cmd/flux-run-prolog \
  src/cmd/flux-run-epilog \
  doc/Makefile \
  doc/test/Makefile \
  t/Makefile \
  t/fluxometer/conf.lua \
  t/fluxometer/conf.lua.installed \
  src/test/docker/poison-libflux.sh
)

AC_CONFIG_LINKS([ \
  t/fluxometer.lua:t/fluxometer.lua \
])

AC_OUTPUT

AS_IF([test "x$enable_docs" != "xno"], [
  if test "x$sphinx" = "xfalse"; then
    AC_MSG_WARN([Python Sphinx not found. Manual pages will not be generated.])
  elif test "x$docutils" = "xfalse"; then
    AC_MSG_WARN([Python Docutils not found. Manual pages will not be generated.])
  fi
])

