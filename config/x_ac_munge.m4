AC_DEFUN([X_AC_MUNGE], [
    X_AC_CHECK_COND_LIB(munge, munge_ctx_create)
    AS_VAR_IF([ac_cv_lib_munge_munge_ctx_create],[yes],,[
        AC_MSG_ERROR([no suitable MUNGE library found])
    ])
  ]
)
