# It is fatal if --enable-pmix-bootstrap and pmix package not found.
# (PKG_CHECK_MODULES default behavior is to fail if package not found)
AC_DEFUN([X_AC_PMIX], [
    AC_ARG_ENABLE([pmix-bootstrap],
        AS_HELP_STRING([--enable-pmix-bootstrap], [Enable PMIx bootstrap]))
    AS_IF([test "x$enable_pmix_bootstrap" = "xyes"], [
        PKG_CHECK_MODULES([PMIX], [pmix])
        AC_DEFINE([HAVE_LIBPMIX], [1], [Enable PMIx bootstrap])
    ])
])
