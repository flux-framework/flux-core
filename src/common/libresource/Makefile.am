AM_CFLAGS = \
        $(WARNING_CFLAGS) \
        $(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
        $(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux \
	$(ZMQ_CFLAGS) $(JANSSON_CFLAGS)

noinst_LTLIBRARIES = \
	libresource.la

libresource_la_SOURCES = \
	rset.h \
	rset.c

test_ldadd = \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(ZMQ_LIBS) $(LIBPTHREAD) $(JANSSON_LIBS)

test_cppflags = \
	$(AM_CPPFLAGS)

TESTS = \
	test_rset.t

check_PROGRAMS = \
	$(TESTS)

test_rset_t_SOURCES = \
	rset.c \
	rset.h \
	test/rset.c
test_rset_t_CPPFLAGS = \
        $(test_cppflags)
test_rset_t_LDADD = \
        $(test_ldadd)


