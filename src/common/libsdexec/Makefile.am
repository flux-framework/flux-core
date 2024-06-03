AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux \
	-I$(top_srcdir)/src/common/libccan \
	$(JANSSON_CFLAGS)

noinst_LTLIBRARIES = libsdexec.la

libsdexec_la_SOURCES = \
	state.c \
	state.h \
	start.c \
	start.h \
	stop.h \
	stop.c \
	list.h \
	list.c \
	property.h \
	property.c \
	outbuf.h \
	outbuf.c \
	channel.h \
	channel.c \
	unit.h \
	unit.c \
	parse.h \
	parse.c

test_ldadd = \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/common/libsubprocess/libsubprocess.la \
	$(top_builddir)/src/common/libtestutil/libtestutil.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(LIBPTHREAD) \
	$(JANSSON_LIBS)

test_cppflags = \
	$(AM_CPPFLAGS)

test_ldflags = \
	-no-install

TESTS = \
	test_parse.t \
	test_channel.t \
	test_channel_outbuf.t \
	test_list.t \
	test_property.t \
	test_start.t \
	test_state.t \
	test_stop.t \
	test_unit.t

check_PROGRAMS = \
	$(TESTS)

test_parse_t_SOURCES = test/parse.c
test_parse_t_CPPFLAGS = $(test_cppflags)
test_parse_t_LDADD = libsdexec.la $(test_ldadd)
test_parse_t_LDFLAGS = $(test_ldflags)

test_channel_t_SOURCES = test/channel.c
test_channel_t_CPPFLAGS = $(test_cppflags)
test_channel_t_LDADD = libsdexec.la $(test_ldadd)
test_channel_t_LDFLAGS = $(test_ldflags)

test_channel_outbuf_t_SOURCES = test/channel_outbuf.c
test_channel_outbuf_t_CPPFLAGS = $(test_cppflags)
test_channel_outbuf_t_LDADD = libsdexec.la $(test_ldadd)
test_channel_outbuf_t_LDFLAGS = $(test_ldflags)

test_list_t_SOURCES = test/list.c
test_list_t_CPPFLAGS = $(test_cppflags)
test_list_t_LDADD = libsdexec.la $(test_ldadd)
test_list_t_LDFLAGS = $(test_ldflags)

test_property_t_SOURCES = test/property.c
test_property_t_CPPFLAGS = $(test_cppflags)
test_property_t_LDADD = libsdexec.la $(test_ldadd)
test_property_t_LDFLAGS = $(test_ldflags)

test_start_t_SOURCES = test/start.c
test_start_t_CPPFLAGS = $(test_cppflags)
test_start_t_LDADD = libsdexec.la $(test_ldadd)
test_start_t_LDFLAGS = $(test_ldflags)

test_state_t_SOURCES = test/state.c
test_state_t_CPPFLAGS = $(test_cppflags)
test_state_t_LDADD = libsdexec.la $(test_ldadd)
test_state_t_LDFLAGS = $(test_ldflags)

test_stop_t_SOURCES = test/stop.c
test_stop_t_CPPFLAGS = $(test_cppflags)
test_stop_t_LDADD = libsdexec.la $(test_ldadd)
test_stop_t_LDFLAGS = $(test_ldflags)

test_unit_t_SOURCES = test/unit.c
test_unit_t_CPPFLAGS = $(test_cppflags)
test_unit_t_LDADD = libsdexec.la $(test_ldadd)
test_unit_t_LDFLAGS = $(test_ldflags)
