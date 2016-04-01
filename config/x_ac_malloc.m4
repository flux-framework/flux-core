AC_DEFUN([X_AC_MALLOC], [

got_tcmalloc=no
got_jemalloc=no

AC_ARG_WITH([tcmalloc],
  [AS_HELP_STRING([--with-tcmalloc], [build with Google-perftools malloc])],
  [], [with_tcmalloc=no])
AC_ARG_WITH([jemalloc],
  [AS_HELP_STRING([--with-jemalloc], [build with jemalloc])],
  [], [with_jemalloc=no])

if test "x$with_tcmalloc" = "xyes" -a "x$with_jemalloc" = "xyes"; then
   AC_MSG_ERROR([cannot configure both --with-tcmalloc and --with-jemalloc])
elif test "x$with_tcmalloc" = "xyes"; then
  AC_CHECK_LIB(tcmalloc, tc_cfree)
  if test "x$ac_cv_lib_tcmalloc_tc_cfree" = "xyes"; then
    got_tcmalloc=yes
    AC_DEFINE([WITH_TCMALLOC], [1], [build with Google-perftools malloc])
    AC_CHECK_HEADERS([gperftools/heap-profiler.h google/heap-profiler.h])
  else
    AC_MSG_ERROR([Google-perftools malloc wanted but not available])
  fi
elif test "x$with_jemalloc" = "xyes"; then
  AC_CHECK_LIB(jemalloc, malloc_stats_print)
  if test "x$ac_cv_lib_jemalloc_malloc_stats_print" = "xyes"; then
    got_jemalloc=yes
    AC_DEFINE([WITH_JEMALLOC], [1], [build with jemalloc])
    AC_CHECK_HEADERS([jemalloc.h])
  else
    AC_MSG_ERROR([jemalloc wanted but not available])
  fi
fi

])
