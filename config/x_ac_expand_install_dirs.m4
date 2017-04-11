##*****************************************************************************
## $Id: x_ac_expand_install_dirs.m4 494 2006-05-08 22:59:28Z dun $
##*****************************************************************************
#  AUTHOR:
#    Chris Dunlap <cdunlap@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_EXPAND_INSTALL_DIRS
#
#  DESCRIPTION:
#    Expand the installation directory variables.
##*****************************************************************************

AC_DEFUN([X_AC_EXPAND_INSTALL_DIRS], [
  AC_MSG_CHECKING([installation directory variables])

  _x_ac_expand_install_dirs_prefix="$prefix"
  test "$prefix" = NONE && prefix="$ac_default_prefix"
  _x_ac_expand_install_dirs_exec_prefix="$exec_prefix"
  test "$exec_prefix" = NONE && exec_prefix="$prefix"

  adl_RECURSIVE_EVAL(["$prefix"], [X_PREFIX])
  AC_DEFINE_UNQUOTED([X_PREFIX], ["$X_PREFIX"],
    [Expansion of the "prefix" installation directory.])
  AC_SUBST([X_PREFIX])

  adl_RECURSIVE_EVAL(["$exec_prefix"], [X_EXEC_PREFIX])
  AC_DEFINE_UNQUOTED([X_EXEC_PREFIX], ["$X_EXEC_PREFIX"],
    [Expansion of the "exec_prefix" installation directory.])
  AC_SUBST([X_EXEC_PREFIX])

  adl_RECURSIVE_EVAL(["$bindir"], [X_BINDIR])
  AC_DEFINE_UNQUOTED([X_BINDIR], ["$X_BINDIR"],
    [Expansion of the "bindir" installation directory.])
  AC_SUBST([X_BINDIR])

  adl_RECURSIVE_EVAL(["$sbindir"], [X_SBINDIR])
  AC_DEFINE_UNQUOTED([X_SBINDIR], ["$X_SBINDIR"],
    [Expansion of the "sbindir" installation directory.])
  AC_SUBST([X_SBINDIR])

  adl_RECURSIVE_EVAL(["$libexecdir"], [X_LIBEXECDIR])
  AC_DEFINE_UNQUOTED([X_LIBEXECDIR], ["$X_LIBEXECDIR"],
    [Expansion of the "libexecdir" installation directory.])
  AC_SUBST([X_LIBEXECDIR])

  adl_RECURSIVE_EVAL(["$datadir"], [X_DATADIR])
  AC_DEFINE_UNQUOTED([X_DATADIR], ["$X_DATADIR"],
    [Expansion of the "datadir" installation directory.])
  AC_SUBST([X_DATADIR])

  adl_RECURSIVE_EVAL(["$sysconfdir"], [X_SYSCONFDIR])
  AC_DEFINE_UNQUOTED([X_SYSCONFDIR], ["$X_SYSCONFDIR"],
    [Expansion of the "sysconfdir" installation directory.])
  AC_SUBST([X_SYSCONFDIR])

  adl_RECURSIVE_EVAL(["$sharedstatedir"], [X_SHAREDSTATEDIR])
  AC_DEFINE_UNQUOTED([X_SHAREDSTATEDIR], ["$X_SHAREDSTATEDIR"],
    [Expansion of the "sharedstatedir" installation directory.])
  AC_SUBST([X_SHAREDSTATEDIR])

  adl_RECURSIVE_EVAL(["$localstatedir"], [X_LOCALSTATEDIR])
  AC_DEFINE_UNQUOTED([X_LOCALSTATEDIR], ["$X_LOCALSTATEDIR"],
    [Expansion of the "localstatedir" installation directory.])
  AC_SUBST([X_LOCALSTATEDIR])

  adl_RECURSIVE_EVAL(["$runstatedir"], [X_RUNSTATEDIR])
  AC_DEFINE_UNQUOTED([X_RUNSTATEDIR], ["$X_RUNSTATEDIR"],
    [Expansion of the "runstatedir" installation directory.])
  AC_SUBST([X_RUNSTATEDIR])

  adl_RECURSIVE_EVAL(["$libdir"], [X_LIBDIR])
  AC_DEFINE_UNQUOTED([X_LIBDIR], ["$X_LIBDIR"],
    [Expansion of the "libdir" installation directory.])
  AC_SUBST([X_LIBDIR])

  adl_RECURSIVE_EVAL(["$includedir"], [X_INCLUDEDIR])
  AC_DEFINE_UNQUOTED([X_INCLUDEDIR], ["$X_INCLUDEDIR"],
    [Expansion of the "includedir" installation directory.])
  AC_SUBST([X_INCLUDEDIR])

  adl_RECURSIVE_EVAL(["$oldincludedir"], [X_OLDINCLUDEDIR])
  AC_DEFINE_UNQUOTED([X_OLDINCLUDEDIR], ["$X_OLDINCLUDEDIR"],
    [Expansion of the "oldincludedir" installation directory.])
  AC_SUBST([X_OLDINCLUDEDIR])

  adl_RECURSIVE_EVAL(["$infodir"], [X_INFODIR])
  AC_DEFINE_UNQUOTED([X_INFODIR], ["$X_INFODIR"],
    [Expansion of the "infodir" installation directory.])
  AC_SUBST([X_INFODIR])

  adl_RECURSIVE_EVAL(["$mandir"], [X_MANDIR])
  AC_DEFINE_UNQUOTED([X_MANDIR], ["$X_MANDIR"],
    [Expansion of the "mandir" installation directory.])
  AC_SUBST([X_MANDIR])

  prefix="$_x_ac_expand_install_dirs_prefix"
  exec_prefix="$_x_ac_expand_install_dirs_exec_prefix"

  AC_MSG_RESULT([yes])
])
