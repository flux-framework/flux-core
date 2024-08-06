# N.B oldest in CI are focal=libzmq-4.3.2-2, centos7=zeromq-4.1.4

AC_DEFUN([X_AC_ZEROMQ], [

    PKG_CHECK_MODULES([ZMQ], [libzmq >= 4.0.4])

    old_CFLAGS=$CFLAGS
    CFLAGS="$CFLAGS $ZMQ_CFLAGS"

    old_LIBS=$LIBS
    LIBS="$LIBS $ZMQ_LIBS"

    AC_MSG_CHECKING([whether zeromq has CURVE support])
    AC_RUN_IFELSE([
        AC_LANG_SOURCE([[
            #include <zmq.h>
	    int main () {
	        char pub[41];
	        char sec[41];
	        if (zmq_curve_keypair (pub, sec) < 0) return 1;
	        return 0;
	    }
	]])],
	[have_zmq_curve_support=yes; AC_MSG_RESULT([yes])],
	[AC_MSG_RESULT([no])]
    )

    CFLAGS=$old_CFLAGS
    LIBS=$old_LIBS

    AS_IF([test "x$have_zmq_curve_support" != "xyes"],
        [AC_MSG_ERROR([zeromq CURVE/libsodium support is required])]
    )
  ]
)
