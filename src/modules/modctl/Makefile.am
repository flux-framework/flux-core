AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

#
# Comms module
#
fluxmod_LTLIBRARIES = modctl.la

fluxmod_libadd = \
	$(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ)

general_ldflags = --disable-static -avoid-version -shared -export-dynamic \
	$(top_builddir)/src/modules/libmrpc/libmrpc.la \
	-L$(top_builddir)/src/common -lflux-core \
	-L$(top_builddir)/src/modules/kvs -lkvs
fluxmod_ldflags =  -module \
        -export-symbols-regex '^mod_(main|name)$$' \
	$(general_ldflags)

modctl_la_SOURCES = \
	modctl.c \
	proto.c \
	proto.h
modctl_la_LDFLAGS = $(fluxmod_ldflags)
modctl_la_LIBADD = $(fluxmod_libadd)

#
# API for module
#
fluxinclude_HEADERS = modctl.h
fluxlib_LTLIBRARIES = libmodctl.la
libmodctl_la_SOURCES = libmodctl.c proto.c
libmodctl_la_LDFLAGS = $(general_ldflags)

#
# Unit tests
#
TESTS = \
	test_proto.t

test_ldadd = \
        $(top_builddir)/src/modules/modctl/libmodctl.la \
        $(top_builddir)/src/common/libutil/libutil.la \
        $(top_builddir)/src/common/libtap/libtap.la \
        $(top_builddir)/src/common/liblsd/liblsd.la \
        $(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ) $(LIBPTHREAD)

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
