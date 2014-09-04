#
# FIXME - this is just a placeholder for a real zeromq/czmq macro
#
AC_DEFUN([X_AC_ZEROMQ], [
    CPPFLAGS="$CPPFLAGS -I/usr/include/czmq"
    X_AC_CHECK_COND_LIB(zmq, zmq_init)
    X_AC_CHECK_COND_LIB(czmq, zhash_new)
  ]
)
