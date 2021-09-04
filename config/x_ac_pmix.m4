# It is fatal if --enable-pmix-* and pmix package not found.
# (PKG_CHECK_MODULES default behavior is to fail if package not found)
AC_DEFUN([X_AC_PMIX], [
    AC_ARG_ENABLE([pmix-bootstrap],
        AS_HELP_STRING([--enable-pmix-bootstrap], [Enable flux broker to bootstrap with pmix, if offered by foreign resource manager]))
    AC_ARG_ENABLE([pmix-shell-plugin],
        AS_HELP_STRING([--enable-pmix-shell-plugin], [Enable flux shell to offer pmix service to select openmpi jobs]))
    AS_IF([test "x$enable_pmix_bootstrap" = "xyes"], [
        AC_DEFINE([BROKER_PMIX], [1], [Enable flux broker pmix bootstrap])
    ])
    AS_IF([test "x$enable_pmix_shell_plugin" = "xyes"], [
        AC_DEFINE([SHELL_PMIX], [1], [Enable flux shell pmix plugin])
    ])

    AS_IF([test "x$enable_pmix_bootstrap" = "xyes" -o "x$enable_pmix_shell_plugin" = "xyes"], [
        PKG_CHECK_MODULES([PMIX], [pmix])
    ])
    AM_CONDITIONAL([BROKER_PMIX], [test "x$enable_pmix_bootstrap" = "xyes"])
    AM_CONDITIONAL([SHELL_PMIX], [test "x$enable_pmix_shell_plugin" = "xyes"])
])
