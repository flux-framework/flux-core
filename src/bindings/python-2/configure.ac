#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])
AC_INIT([flux-python-2],
        m4_esyscmd([git describe --always | awk '/.*/ {sub(/^v/, ""); printf "%s",$1; exit}']))
# AC_CONFIG_SRCDIR([NEWS])
# AC_CONFIG_HEADERS([config.h])
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_MACRO_DIR([config])
AM_INIT_AUTOMAKE([subdir-objects])
AM_SILENT_RULES([yes])

#LT_PREREQ([2.4.2])
LT_INIT([dlopen])

# Checks for programs.
AC_DEFINE([_GNU_SOURCE], 1,
          [Define _GNU_SOURCE so that we get all necessary prototypes])
AC_PROG_CC_C99
AC_PROG_LN_S
AC_PROG_MAKE_SET
AM_PROG_CC_C_O

if test "$GCC" = yes; then
  WARNING_CFLAGS="-Wall -Werror -Werror=missing-field-initializers -Wno-error=deprecated-declarations"
  AC_SUBST([WARNING_CFLAGS])
fi
if test "$GXX" = "yes"; then
  WARNING_CXXFLAGS=$WARNING_CFLAGS
  AC_SUBST([WARNING_CXXFLAGS])
fi

# Checks for libraries.
PKG_PROG_PKG_CONFIG

#  Edit PATH to remove $PWD/src/cmd so that AM_PATH_PYTHON doesn't find
#  flux python script (thus creating a link to itself.) This needs to be
#  done *before* AX_PYTHON_DEVEL.
#
saved_PATH=$PATH
export PATH=$(echo $PATH | sed "s|$(pwd)/src/cmd:*||")

AC_ARG_WITH([python2], AS_HELP_STRING([--with-python2],
             [Build python2 bindings along with the required python3]))

AS_IF([test "x$with_python2" = "xyes"], [
  export PYTHON_NOVERSIONCHECK=yes
])
AM_CONDITIONAL([BUILD_WITH_PYTHON2], [test "x$with_python2" = "xyes"])
# avoid pollution from outer env vars
PYTHON=$PYTHON2
PYTHON_VERSION=$PYTHON2_VERSION
AX_PYTHON_DEVEL([>='2.7'])

AM_PATH_PYTHON([$ac_python_version])
if test "X$PYTHON" = "X"; then
  AC_MSG_ERROR([could not find python])
fi
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
AS_VAR_SET(flyxpy2dir, $pyexecdir/flux)
AC_SUBST(flyxpy2dir)
AS_VAR_SET(flyxpy2sodir, $pyexecdir/_flux)
AC_SUBST(flyxpy2sodir)
AS_VAR_SET(flyxpy2moddir, $pyexecdir/flux/modules)
AC_SUBST(flyxpy2moddir)
AS_VAR_SET(fluxlibdir, $libdir/flux)
AC_SUBST(fluxlibdir)
# Target of PYTHONPATH set by flux(1) cmddriver, so flux(1)
# doesn't inadvertently insert system python paths (or any
# other python path for that matter) first in PYTHONPATH.
#
AS_VAR_SET(flyxpy2linkdir, $fluxlibdir/python$PYTHON_VERSION)
AC_SUBST(flyxpy2linkdir)

AC_SUBST(PYTHON_LIBRARY, lib${ac_python_library}.so)

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



AC_CONFIG_FILES([Makefile
  flux/Makefile
  flux/core/Makefile
  _flux/Makefile
  t/Makefile
  ])
AC_OUTPUT

echo "
  $PACKAGE_NAME version $PACKAGE_VERSION
  Prefix...........: $prefix
  Debug Build......: $debug
  C Compiler.......: $CC
  C++ Compiler.....: $CXX
  CFLAGS...........: $CFLAGS
  CPPFLAGS.......... $CPPFLAGS
  CXXFLAGS.......... $CXXFLAGS
  FLUX.............: $FLUX
  FLUX_CORE_CFLAGS.: $FLUX_CORE_CFLAGS
  FLUX_CORE_LIBS...: $FLUX_CORE_LIBS
  FLUX_PREFIX......: $FLUX_PREFIX
  LDFLAGS..........: $LDFLAGS
  LIBS.............: $LIBS
  Linker...........: $LD
"
