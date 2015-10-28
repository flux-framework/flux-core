AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

#
# Comms module
#
fluxmod_LTLIBRARIES = modctl.la

modctl_la_SOURCES = \
	modctl.c \
	proto.c \
	proto.h
modctl_la_LDFLAGS = -avoid-version -module -shared -export-dynamic \
		    -export-symbols-regex '^mod_(main|name)$$'
modctl_la_LIBADD = $(top_builddir)/src/common/libflux-internal.la \
		   $(top_builddir)/src/common/libflux-core.la \
		   $(top_builddir)/src/modules/libmrpc/libmrpc.la \
		   $(JSON_LIBS) $(ZMQ_LIBS)

#
# API for module
#
fluxinclude_HEADERS = modctl.h
fluxlib_LTLIBRARIES = libmodctl.la
libmodctl_la_SOURCES = libmodctl.c proto.c
libmodctl_la_LDFLAGS = -shared -export-dynamic --disable-static \
		       -Wl,--no-undefined \
		       -export-symbols-regex '^flux_.*'
libmodctl_la_LIBADD = $(top_builddir)/src/common/libflux-internal.la \
		      $(top_builddir)/src/common/libflux-core.la \
		      $(top_builddir)/src/modules/libmrpc/libmrpc.la

#
# Unit tests
#
TESTS = \
	test_proto.t

test_ldadd = \
        $(top_builddir)/src/modules/modctl/proto.o \
        $(top_builddir)/src/modules/modctl/libmodctl.la \
	$(top_builddir)/src/common/libflux-internal.la \
        $(top_builddir)/src/common/libtap/libtap.la \
	$(LIBMUNGE) $(JSON_LIBS) $(ZMQ_LIBS) $(LIBPTHREAD) $(LIBUTIL) \
	$(LIBDL) $(LIBRT)

test_cppflags = \
        -I$(top_srcdir)/src/common/libtap \
        $(AM_CPPFLAGS)

check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
       $(top_srcdir)/config/tap-driver.sh

test_proto_t_SOURCES = test/proto.c
test_proto_t_CPPFLAGS = $(test_cppflags)
test_proto_t_LDADD = $(test_ldadd)
