# It is fatal if --enable-pmix-* and pmix package not found.
# (PKG_CHECK_MODULES default behavior is to fail if package not found)
AC_DEFUN([X_AC_PMIX], [
    AC_ARG_ENABLE([pmix-bootstrap],
        AS_HELP_STRING([--enable-pmix-bootstrap], [Enable flux broker to bootstrap with pmix, if offered by foreign resource manager]))
    AS_IF([test "x$enable_pmix_bootstrap" = "xyes"], [
        AC_DEFINE([BROKER_PMIX], [1], [Enable flux broker pmix bootstrap])
        PKG_CHECK_MODULES([PMIX], [pmix])
    ])
    AM_CONDITIONAL([BROKER_PMIX], [test "x$enable_pmix_bootstrap" = "xyes"])
])
