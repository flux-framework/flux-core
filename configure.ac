##
# Prologue
##
AC_INIT(m4_esyscmd([awk '/Name:/ {printf "%s",$2; exit}' META]),
        m4_esyscmd([awk '/Version:/ {printf "%s",$2; exit}' META]))
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_SRCDIR([NEWS])
AC_CANONICAL_SYSTEM
X_AC_META
X_AC_EXPAND_INSTALL_DIRS

##
# Automake support
##
AM_INIT_AUTOMAKE([subdir-objects])
AM_CONFIG_HEADER([config/config.h])
AM_MAINTAINER_MODE

AC_DEFINE([_GNU_SOURCE], 1,
          [Define _GNU_SOURCE so that we get all necessary prototypes])

##
# Checks for programs
##
AC_PROG_CC
AM_PROG_CC_C_O
if test "$GCC" = yes; then
  GCCWARN="-Wall"
  AC_SUBST([GCCWARN])
fi
AC_PROG_RANLIB
AC_PROG_LIBTOOL
AC_LIB_LTDL

##
# Checks for header files.
##
AC_HEADER_STDC
AC_CHECK_HEADERS( \
  pthread.h \
  getopt.h \
)
# czmq headers might be in /usr/include/czmq subdir, or not
AC_CHECK_HEADERS( \
  czmq.h \
  czmq/czmq.h \
)
AC_CHECK_HEADERS( \
  slurm/pmi.h \
)

##
# Checks for typedefs, structures, and compiler characteristics
## 
AC_C_BIGENDIAN
AC_C_CONST

##
# Checks for library functions
##
AC_CHECK_FUNCS( \
  getopt_long \
  vsnprintf \
  vsscanf \
)
X_AC_CHECK_PTHREADS
X_AC_CHECK_COND_LIB(munge, munge_ctx_create)
X_AC_CHECK_COND_LIB(json, json_object_get)
X_AC_CHECK_COND_LIB(util, forkpty)
X_AC_CHECK_COND_LIB(rt, clock_gettime)
AX_LUA_HEADERS
AX_LUA_HEADERS(501)
AX_LUA_LIBS
X_AC_ZEROMQ

##
# For list.c, hostlist.c, hash.c
##
AC_DEFINE(WITH_PTHREADS, 1, [Make liblsd thread safe])

AC_SYS_LARGEFILE

##
# Epilogue
##
AC_CONFIG_FILES( \
  Makefile \
  flux-core.spec \
  src/Makefile \
  src/common/Makefile \
  src/common/liblsd/Makefile \
  src/common/libutil/Makefile \
  src/common/libzio/Makefile \
  src/lib/Makefile \
  src/lib/libflux/Makefile \
  src/broker/Makefile \
  src/cmd/Makefile \
  src/modules/Makefile \
  src/modules/api/Makefile \
  src/modules/kvs/Makefile \
  src/modules/modctl/Makefile \
  src/modules/live/Makefile \
)

AC_OUTPUT
