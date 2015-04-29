AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	-DCONNECTOR_PATH=\"$(fluxconnectordir)\"

fluxcoreinclude_HEADERS = \
	flux.h \
	handle.h \
	connector.h \
	reactor.h \
	reduce.h \
	security.h \
	message.h \
	request.h \
	response.h \
	rpc.h \
	panic.h \
	event.h \
	module.h \
	reparent.h \
	info.h \
	flog.h \
	conf.h \
	tmpdir.h

noinst_LTLIBRARIES = \
	libflux.la

libflux_la_SOURCES = \
	flog.c \
	info.c \
	handle.c \
	reactor.c \
	reduce.c \
	security.c \
	message.c \
	request.c \
	response.c \
	rpc.c \
	panic.c \
	event.c \
	module.c \
	reparent.c \
	conf.c \
	tagpool.h \
	tagpool.c \
	ev_flux.h \
	ev_flux.c \
	tmpdir.c

libflux_la_LDFLAGS = -avoid-version -module -shared -export-dynamic

TESTS = test_conf.t \
	test_module.t \
	test_message.t \
	test_request.t \
	test_response.t \
	test_event.t \
	test_tagpool.t

test_ldadd = \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(top_builddir)/src/common/libev/libev.la \
	$(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ) $(LIBPTHREAD)

test_cppflags = \
	-DMODULE_PATH=\"$(top_builddir)/src/modules\" \
        -I$(top_srcdir)/src/common/libtap \
        $(AM_CPPFLAGS)

check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
       $(top_srcdir)/config/tap-driver.sh

test_conf_t_SOURCES = test/conf.c
test_conf_t_CPPFLAGS = $(test_cppflags)
test_conf_t_LDADD = $(test_ldadd)

test_module_t_SOURCES = test/module.c
test_module_t_CPPFLAGS = $(test_cppflags)
test_module_t_LDADD = $(test_ldadd) $(LIBDL)
test_module_t_DEPENDENCIES = $(top_builddir)/src/modules/kvs/kvs.la

$(test_module_t_DEPENDENCIES):
	@cd `dirname $@` && $(MAKE)


test_message_t_SOURCES = test/message.c
test_message_t_CPPFLAGS = $(test_cppflags)
test_message_t_LDADD = $(test_ldadd) $(LIBDL)

test_event_t_SOURCES = test/event.c
test_event_t_CPPFLAGS = $(test_cppflags)
test_event_t_LDADD = $(test_ldadd) $(LIBDL)

test_tagpool_t_SOURCES = test/tagpool.c
test_tagpool_t_CPPFLAGS = $(test_cppflags)
test_tagpool_t_LDADD = $(test_ldadd) $(LIBDL)

test_request_t_SOURCES = test/request.c
test_request_t_CPPFLAGS = $(test_cppflags)
test_request_t_LDADD = $(test_ldadd) $(LIBDL)

test_response_t_SOURCES = test/response.c
test_response_t_CPPFLAGS = $(test_cppflags)
test_response_t_LDADD = $(test_ldadd) $(LIBDL)
