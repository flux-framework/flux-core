# N.B oldest in CI are focal=libzmq-4.3.2-2, centos7=zeromq-4.1.4

AC_DEFUN([X_AC_ZEROMQ], [

    PKG_CHECK_MODULES([ZMQ], [libzmq >= 4.0.4])

  ]
)
