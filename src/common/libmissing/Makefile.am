# libmissing is for functions that may be missing from standard libraries
# on some, but not all, platforms.  To handle a missing function named 'doit':
#
# 1. add 'doit' to AC_REPLACE_FUNCS in configure.ac
# 2. add 'doit.c' and 'doit.h' to this directory
# 3. add 'doit.h' to EXTRA_libmissing_la_SOURCES
#
# AC_REPLACE_FUNCS takes care of adding HAVE_DOIT to config.h,
# doit.lo to LTLIBOBJS, and doit.c to DISTFILES.
#
# Where the missing function is used, add something like
#
#   #ifndef HAVE_DOIT
#   #include "src/common/libmissing/doit.h"
#   #endif
#
# or if there is an existing system include just for the missing function
#
#   #ifdef HAVE_DOIT
#   #include <doit.h>
#   #else
#   #include "src/common/libmissing/doit.h"
#   #endif
#
# Make sure the replacement function is compatible with the project license
# and has proper attribution in the commit message that adds it.
#
# See also:
# https://www.gnu.org/software/automake/manual/html_node/LIBOBJS.html
# https://www.gnu.org/savannah-checkouts/gnu/autoconf/manual/autoconf-2.71/html_node/Generic-Functions.html
#
AM_CFLAGS = \
        $(WARNING_CFLAGS) \
        $(CODE_COVERAGE_CFLAGS)

AM_CPPFLAGS = \
	$(CODE_COVERAGE_CPPFLAGS)

noinst_LTLIBRARIES = libmissing.la

libmissing_la_LIBADD = $(LTLIBOBJS)
libmissing_la_SOURCES =

EXTRA_libmissing_la_SOURCES = \
	strlcpy.h \
	strlcat.h \
	argz.h \
	argz.c \
	envz.h \
	envz.c \
	macros.h \
	strerrorname_np.h \
	json_object_update_recursive.h \
	pipe2.h \
	mempcpy.h
