# If configured "--with-pmix" locate libpmix.so.
# It is a hard failure if pmix is requested but cannot be found.
# The default is to build Flux without libpmix support.
#
AC_DEFUN([X_AC_PMIX], [
    AC_ARG_WITH([pmix],
        AS_HELP_STRING([--with-pmix], [Enable Flux bootstrap with PMIx]))
    AS_IF([test "x$with_pmix" = "xyes"], [
        X_AC_CHECK_COND_LIB(pmix, PMIx_Init)
        AS_VAR_IF([ac_cv_lib_pmix_PMIx_Init],[yes],,[
            AC_MSG_ERROR([no suitable PMIX library found])
        ])
    ])
    AM_CONDITIONAL([HAVE_LIBPMIX], [test "x$with_pmix" = "xyes"])
  ]
)
