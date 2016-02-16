AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS)

#
# Comms module
#
fluxmod_LTLIBRARIES = modctl.la

modctl_la_SOURCES = \
	modctl.c \
	proto.c \
	proto.h
modctl_la_LDFLAGS = $(fluxmod_ldflags) -module
modctl_la_LIBADD = $(top_builddir)/src/common/libflux-internal.la \
		   $(top_builddir)/src/common/libflux-core.la \
		   $(top_builddir)/src/modules/libmrpc/libflux-mrpc.la \
		   $(JSON_LIBS) $(ZMQ_LIBS)

#
# API for module
#
fluxcoreinclude_HEADERS = modctl.h
lib_LTLIBRARIES = libflux-modctl.la
libflux_modctl_la_SOURCES = libmodctl.c proto.c
libflux_modctl_la_LDFLAGS = $(fluxlib_ldflags) \
			    -export-symbols-regex '^flux_.*'

libflux_modctl_la_LIBADD = $(top_builddir)/src/common/libflux-internal.la \
			   $(top_builddir)/src/common/libflux-core.la \
			   $(top_builddir)/src/modules/libmrpc/libflux-mrpc.la

#
# Unit tests
#
TESTS = \
	test_proto.t

test_ldadd = \
        $(top_builddir)/src/modules/modctl/proto.o \
        $(top_builddir)/src/modules/modctl/libflux-modctl.la \
	$(top_builddir)/src/common/libflux-internal.la \
        $(top_builddir)/src/common/libtap/libtap.la \
	$(LIBMUNGE) $(JSON_LIBS) $(ZMQ_LIBS) $(LIBPTHREAD) $(LIBUTIL) \
	$(LIBDL) $(LIBRT)

test_cppflags = \
        $(AM_CPPFLAGS) \
        -I$(top_srcdir)/src/common/libtap

check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
       $(top_srcdir)/config/tap-driver.sh

test_proto_t_SOURCES = test/proto.c
test_proto_t_CPPFLAGS = $(test_cppflags)
test_proto_t_LDADD = $(test_ldadd)
