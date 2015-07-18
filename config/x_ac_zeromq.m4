AC_DEFUN([X_AC_ZEROMQ], [

    PKG_CHECK_MODULES([ZMQ], [libczmq >= 3.0.0 libzmq >= 4.0.4])

    ac_save_LIBS="$LIBS"
    LIBS="$LIBS $ZMQ_LIBS"
    ac_save_CFLAGS="$CFLAGS"
    CFLAGS="$CFLAGS $ZMQ_CFLAGS"

    AC_MSG_CHECKING([For CURVE encryption support in libzmq])
    AC_CACHE_VAL(ac_cv_curve_support,
      AC_RUN_IFELSE([AC_LANG_SOURCE([[
#include <zmq.h>
#include <zmq_utils.h>
  main()
  {
    char x[[41]], y[[41]];
    /*
     * Check for CURVE support in current version of libzmq
     */
    int rc = zmq_curve_keypair (x, y);
    exit (rc >= 0 ? 0 : 1);
  }
]])],
    ac_cv_curve_support=yes,
    ac_cv_curve_support=no,
    ac_cv_curve_support=no))

    AC_MSG_RESULT($ac_cv_curve_support)
    if test "x$ac_cv_curve_support" = "xno"; then
      AC_MSG_ERROR([
 Failed to detect CURVE support in libzmq!
 Perhaps you need to compile with libsodium?])
    fi
    LIBS="$ac_save_LIBS"
    CFLAGS="$ac_save_CFLAGS"

  ]
)
