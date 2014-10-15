AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

fluxcoreinclude_HEADERS = \
	flux.h \
	handle.h \
	handle_impl.h \
	reactor.h \
	reduce.h \
	security.h \
	message.h \
	panic.h \
	event.h \
	request.h \
	module.h \
	reparent.h \
	info.h \
	flog.h \
	conf.h

noinst_LTLIBRARIES = \
	libflux.la

libflux_la_SOURCES = \
	flog.c \
	info.c \
	handle.c \
	reduce.c \
	security.c \
	message.c \
	panic.c \
	event.c \
	request.c \
	module.c \
	reparent.c \
	conf.c

TESTS = test_conf.t

test_ldadd = \
        $(top_builddir)/src/common/libutil/libutil.la \
        $(top_builddir)/src/common/libtap/libtap.la \
        $(top_builddir)/src/common/liblsd/liblsd.la \
        $(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ) $(LIBPTHREAD)

test_cppflags = \
        -DTEST_MAIN \
        -I$(top_srcdir)/src/common/libtap \
        $(AM_CPPFLAGS)

check_PROGRAMS = \
        test_conf.t

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
       $(top_srcdir)/config/tap-driver.sh

test_conf_t_SOURCES = conf.c
test_conf_t_CPPFLAGS = $(test_cppflags)
test_conf_t_LDADD = $(test_ldadd)

