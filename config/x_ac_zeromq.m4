#
# FIXME - this is just a placeholder for a real zeromq/czmq macro
#
AC_DEFUN([X_AC_ZEROMQ], [
    X_AC_CHECK_COND_LIB(zmq, zmq_init)
    AS_VAR_IF([ac_cv_lib_zmq_zmq_init],[yes],,[
        AC_MSG_ERROR([no suitable zmq library found])
    ])

    CPPFLAGS="$CPPFLAGS -I/usr/include/czmq"
    X_AC_CHECK_COND_LIB(czmq, zhash_new)
    AS_VAR_IF([ac_cv_lib_czmq_zhash_new],[yes],,[
        AC_MSG_ERROR([no suitable czmq library found])
    ])
  ]
)
