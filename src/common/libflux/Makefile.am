AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	$(JANSSON_CFLAGS) $(ZMQ_CFLAGS)

installed_conf_cppflags = \
	-DINSTALLED_MODULE_PATH=\"$(fluxmoddir)\" \
	-DINSTALLED_CONNECTOR_PATH=\"$(fluxconnectordir)\" \
	-DINSTALLED_EXEC_PATH=\"$(fluxcmddir)\" \
	-DINSTALLED_LUA_PATH_ADD=\"$(luadir)/?.lua\" \
	-DINSTALLED_LUA_CPATH_ADD=\"$(luaexecdir)/?.so\" \
	-DINSTALLED_PYTHON_PATH=\"$(pyexecdir)\" \
	-DINSTALLED_MAN_PATH=\"$(mandir)\" \
	-DINSTALLED_PROGRAM_LIBRARY_PATH=\"$(fluxlibdir)\" \
	-DINSTALLED_PMI_LIBRARY_PATH=\"$(fluxlibdir)/libpmi.so\" \
	-DINSTALLED_RC1_PATH=\"$(fluxrcdir)/rc1\" \
	-DINSTALLED_RC3_PATH=\"$(fluxrcdir)/rc3\" \
	-DINSTALLED_WRECK_LUA_PATTERN=\"$(sysconfdir)/wreck/lua.d/*.lua\" \
	-DINSTALLED_WREXECD_PATH=\"$(fluxlibexecdir)/wrexecd\" \
	-DINSTALLED_CMDHELP_PATTERN=\"${datadir}/flux/help.d/*.json\" \
	-DINSTALLED_NO_DOCS_PATH=\"${datadir}/flux/.nodocs\" \
	-DINSTALLED_RUNDIR=\"${runstatedir}/flux\" \
	-DINSTALLED_BINDIR=\"$(fluxcmddir)\"

intree_conf_cppflags = \
	-DINTREE_MODULE_PATH=\"$(abs_top_builddir)/src/modules\" \
	-DINTREE_CONNECTOR_PATH=\"$(abs_top_builddir)/src/connectors\" \
	-DINTREE_EXEC_PATH=\"$(abs_top_builddir)/src/cmd:$(abs_top_srcdir)/src/cmd:$(abs_top_builddir)/src/broker\" \
	-DINTREE_LUA_PATH_ADD=\"$(abs_top_srcdir)/src/bindings/lua/?.lua\" \
	-DINTREE_LUA_CPATH_ADD=\"$(abs_top_builddir)/src/bindings/lua/?.so\" \
	-DINTREE_PYTHON_PATH=\"$(abs_top_builddir)/src/bindings/python:$(abs_top_srcdir)/src/bindings/python/pycotap\" \
	-DINTREE_MAN_PATH=\"$(abs_top_builddir)/doc\" \
	-DINTREE_PROGRAM_LIBRARY_PATH=\"$(abs_top_builddir)/src/common/.libs\" \
	-DINTREE_PMI_LIBRARY_PATH=\"$(abs_top_builddir)/src/common/.libs/libpmi.so\" \
	-DINTREE_RC1_PATH=\"$(abs_top_srcdir)/etc/rc1\" \
	-DINTREE_RC3_PATH=\"$(abs_top_srcdir)/etc/rc3\" \
	-DINTREE_WRECK_LUA_PATTERN=\"$(abs_top_srcdir)/src/modules/wreck/lua.d/*.lua\" \
	-DINTREE_WREXECD_PATH=\"$(abs_top_builddir)/src/modules/wreck/wrexecd\" \
	-DINTREE_CMDHELP_PATTERN=\"${abs_top_builddir}/etc/flux/help.d/*.json\" \
	-DINTREE_KEYDIR=\"${abs_top_builddir}/etc/flux\" \
	-DINTREE_NO_DOCS_PATH=\"${abs_top_builddir}/etc/flux/.nodocs\" \
	-DINTREE_BINDIR=\"${abs_top_builddir}/src/cmd\"


fluxcoreinclude_HEADERS = \
	flux.h \
	types.h \
	handle.h \
	connector.h \
	reactor.h \
	msg_handler.h \
	reduce.h \
	security.h \
	message.h \
	request.h \
	keepalive.h \
	response.h \
	rpc.h \
	mrpc.h \
	panic.h \
	event.h \
	module.h \
	info.h \
	attr.h \
	flog.h \
	conf.h \
	heartbeat.h \
	content.h \
	future.h \
	barrier.h

noinst_LTLIBRARIES = \
	libflux.la

libflux_la_SOURCES = \
	flog.c \
	info.c \
	attr.c \
	handle.c \
	reactor.c \
	msg_handler.c \
	reduce.c \
	security.c \
	message.c \
	request.c \
	response.c \
	rpc.c \
	mrpc.c \
	panic.c \
	event.c \
	module.c \
	conf.c \
	tagpool.h \
	tagpool.c \
	ev_flux.h \
	ev_flux.c \
	heartbeat.c \
	keepalive.c \
	content.c \
	future.c \
	barrier.c

libflux_la_CPPFLAGS = \
	$(installed_conf_cppflags) \
	$(intree_conf_cppflags) \
	$(AM_CPPFLAGS)
libflux_la_LDFLAGS = -avoid-version -module -shared -export-dynamic

TESTS = test_module.t \
	test_message.t \
	test_request.t \
	test_response.t \
	test_event.t \
	test_tagpool.t \
	test_security.t \
	test_future.t \
	test_reactor.t

test_ldadd = \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/libjson-c/libjson-c.la \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(top_builddir)/src/common/libev/libev.la \
	$(ZMQ_LIBS) $(JANSSON_LIBS) $(LIBPTHREAD) $(LIBMUNGE)

test_cppflags = \
        -I$(top_srcdir)/src/common/libtap \
        $(AM_CPPFLAGS)

check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
       $(top_srcdir)/config/tap-driver.sh

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

test_reactor_t_SOURCES = test/reactor.c
test_reactor_t_CPPFLAGS = $(test_cppflags)
test_reactor_t_LDADD = $(test_ldadd) $(LIBDL)

test_security_t_SOURCES = test/security.c
test_security_t_CPPFLAGS = $(test_cppflags)
test_security_t_LDADD = $(test_ldadd) $(LIBDL)

test_future_t_SOURCES = test/future.c
test_future_t_CPPFLAGS = $(test_cppflags)
test_future_t_LDADD = $(test_ldadd) $(LIBDL)
