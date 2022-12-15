AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_srcdir)/src/common/libccan \
	-I$(top_builddir) \
	-I$(top_builddir)/src/common/libflux \
	$(ZMQ_CFLAGS) \
	$(JANSSON_CFLAGS) \
	$(LIBUUID_CFLAGS)

installed_conf_cppflags = \
	-DINSTALLED_MODULE_PATH=\"$(fluxmoddir)\" \
	-DINSTALLED_CONNECTOR_PATH=\"$(fluxconnectordir)\" \
	-DINSTALLED_EXEC_PATH=\"$(fluxcmddir)\" \
	-DINSTALLED_LUA_PATH_ADD=\"$(luadir)/?.lua\" \
	-DINSTALLED_LUA_CPATH_ADD=\"$(luaexecdir)/?.so\" \
	-DINSTALLED_PYTHON_PATH=\"$(fluxpylinkdir)\" \
	-DINSTALLED_MAN_PATH=\"$(mandir)\" \
	-DINSTALLED_PROGRAM_LIBRARY_PATH=\"$(fluxlibdir)\" \
	-DINSTALLED_PMI_LIBRARY_PATH=\"$(fluxlibdir)/libpmi.so\" \
	-DINSTALLED_RC1_PATH=\"$(fluxrcdir)/rc1\" \
	-DINSTALLED_RC3_PATH=\"$(fluxrcdir)/rc3\" \
	-DINSTALLED_SHELL_PATH=\"$(fluxlibexecdir)/flux-shell\" \
	-DINSTALLED_SHELL_PLUGINPATH=\"$(fluxlibdir)/shell/plugins\" \
	-DINSTALLED_SHELL_INITRC=\"$(fluxrcdir)/shell/initrc.lua\" \
	-DINSTALLED_JOBTAP_PLUGINPATH=\"$(fluxlibdir)/job-manager/plugins\" \
	-DINSTALLED_CMDHELP_PATTERN=\"${datadir}/flux/help.d/*.json\" \
	-DINSTALLED_NO_DOCS_PATH=\"${datadir}/flux/.nodocs\" \
	-DINSTALLED_RUNDIR=\"${runstatedir}/flux\" \
	-DINSTALLED_BINDIR=\"$(fluxcmddir)\"

intree_conf_cppflags = \
	-DINTREE_MODULE_PATH=\"$(abs_top_builddir)/src/modules\" \
	-DINTREE_CONNECTOR_PATH=\"$(abs_top_builddir)/src/connectors\" \
	-DINTREE_EXEC_PATH=\"$(abs_top_builddir)/src/cmd:$(abs_top_srcdir)/src/cmd:$(abs_top_builddir)/src/broker\" \
	-DINTREE_LUA_PATH_ADD=\"$(abs_top_builddir)/t/?.lua\;$(abs_top_srcdir)/src/bindings/lua/?.lua\" \
	-DINTREE_LUA_CPATH_ADD=\"$(abs_top_builddir)/src/bindings/lua/?.so\" \
	-DINTREE_PYTHON_PATH=\"$(abs_top_builddir)/src/bindings/python:$(abs_top_srcdir)/src/bindings/python\" \
	-DINTREE_MAN_PATH=\"$(abs_top_builddir)/doc\" \
	-DINTREE_PROGRAM_LIBRARY_PATH=\"$(abs_top_builddir)/src/common/.libs\" \
	-DINTREE_PMI_LIBRARY_PATH=\"$(abs_top_builddir)/src/common/flux/.libs/libpmi.so\" \
	-DINTREE_RC1_PATH=\"$(abs_top_srcdir)/etc/rc1\" \
	-DINTREE_RC3_PATH=\"$(abs_top_srcdir)/etc/rc3\" \
	-DINTREE_SHELL_PATH=\"$(abs_top_builddir)/src/shell/flux-shell\" \
	-DINTREE_SHELL_PLUGINPATH=\"$(abs_top_builddir)/src/shell/plugins\" \
	-DINTREE_SHELL_INITRC=\"$(abs_top_srcdir)/src/shell/initrc.lua\" \
	-DINTREE_JOBTAP_PLUGINPATH=\"$(abs_top_builddir)/src/modules/job-manager/plugins/.libs\" \
	-DINTREE_CMDHELP_PATTERN=\"${abs_top_builddir}/etc/flux/help.d/*.json\" \
	-DINTREE_NO_DOCS_PATH=\"${abs_top_builddir}/etc/flux/.nodocs\" \
	-DINTREE_BINDIR=\"${abs_top_builddir}/src/cmd\"


fluxcoreinclude_HEADERS = \
	flux.h \
	types.h \
	handle.h \
	connector.h \
	reactor.h \
	msg_handler.h \
	message.h \
	msglist.h \
	request.h \
	control.h \
	response.h \
	rpc.h \
	panic.h \
	event.h \
	module.h \
	attr.h \
	flog.h \
	conf.h \
	future.h \
	barrier.h \
	buffer.h \
	service.h \
	plugin.h \
	sync.h \
	disconnect.h \
	stats.h

nodist_fluxcoreinclude_HEADERS = \
	version.h

noinst_LTLIBRARIES = \
	libflux.la \
	libmessage.la

libflux_la_SOURCES = \
	flog.c \
	attr.c \
	handle.c \
	reactor.c \
	reactor_private.h \
	msg_handler.c \
	message.c \
	msglist.c \
	request.c \
	response.c \
	rpc.c \
	panic.c \
	event.c \
	module.c \
	conf_private.h \
	conf.c \
	tagpool.h \
	tagpool.c \
	ev_flux.h \
	ev_flux.c \
	ev_buffer_read.h \
	ev_buffer_read.c \
	ev_buffer_write.h \
	ev_buffer_write.c \
	control.c \
	future.c \
	composite_future.c \
	barrier.c \
	buffer_private.h \
	buffer.c \
	service.c \
	version.c \
	plugin.c \
	sync.c \
	disconnect.c \
	stats.c \
	fripp.h \
	fripp.c

libflux_la_CPPFLAGS = \
	$(installed_conf_cppflags) \
	$(intree_conf_cppflags) \
	$(AM_CPPFLAGS)
libflux_la_LDFLAGS = -avoid-version -module -shared -export-dynamic

libmessage_la_SOURCES = \
	message_private.h \
	message_iovec.h \
	message_iovec.c \
	message_route.h \
	message_route.c \
	message_proto.h \
	message_proto.c


libmessage_la_CPPFLAGS = $(AM_CPPFLAGS)
libmessage_la_LDFLAGS = -avoid-version -module -shared -export-dynamic

TESTS = test_message.t \
	test_msglist.t \
	test_request.t \
	test_response.t \
	test_event.t \
	test_tagpool.t \
	test_future.t \
	test_composite_future.t \
	test_reactor.t \
	test_buffer.t \
	test_conf.t \
	test_rpc.t \
	test_rpc_chained.t \
	test_handle.t \
	test_msg_handler.t \
	test_version.t \
	test_dispatch.t \
	test_handle.t \
	test_log.t \
	test_reactor_loop.t \
	test_rpc_security.t \
	test_panic.t \
	test_attr.t \
	test_module.t \
	test_plugin.t \
	test_sync.t \
	test_disconnect.t

test_ldadd = \
	$(top_builddir)/src/common/libtestutil/libtestutil.la \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libzmqutil/libzmqutil.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libkvs/libkvs.la \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(ZMQ_LIBS) \
	$(LIBUUID_LIBS) \
	$(JANSSON_LIBS) \
	$(LIBPTHREAD) \
	$(LIBDL)

test_cppflags = \
        -I$(top_srcdir)/src/common/libtap \
        $(AM_CPPFLAGS)

check_LTLIBRARIES = \
	test/module_fake1.la \
	test/module_fake2.la \
	test/plugin_foo.la \
	test/plugin_bar.la


check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
       $(top_srcdir)/config/tap-driver.sh

test_message_t_SOURCES = test/message.c
test_message_t_CPPFLAGS = $(test_cppflags)
test_message_t_LDADD = $(test_ldadd)

test_msglist_t_SOURCES = test/msglist.c
test_msglist_t_CPPFLAGS = $(test_cppflags)
test_msglist_t_LDADD = $(test_ldadd)

test_event_t_SOURCES = test/event.c
test_event_t_CPPFLAGS = $(test_cppflags)
test_event_t_LDADD = $(test_ldadd)

test_tagpool_t_SOURCES = test/tagpool.c
test_tagpool_t_CPPFLAGS = $(test_cppflags)
test_tagpool_t_LDADD = $(test_ldadd)

test_request_t_SOURCES = test/request.c
test_request_t_CPPFLAGS = $(test_cppflags)
test_request_t_LDADD = $(test_ldadd)

test_response_t_SOURCES = test/response.c
test_response_t_CPPFLAGS = $(test_cppflags)
test_response_t_LDADD = $(test_ldadd)

test_reactor_t_SOURCES = test/reactor.c
test_reactor_t_CPPFLAGS = $(test_cppflags)
test_reactor_t_LDADD = $(test_ldadd)

test_future_t_SOURCES = test/future.c
test_future_t_CPPFLAGS = $(test_cppflags)
test_future_t_LDADD = $(test_ldadd)

test_composite_future_t_SOURCES = test/composite_future.c
test_composite_future_t_CPPFLAGS = $(test_cppflags)
test_composite_future_t_LDADD = $(test_ldadd)

test_conf_t_SOURCES = test/conf.c
test_conf_t_CPPFLAGS = $(test_cppflags)
test_conf_t_LDADD = $(test_ldadd)

test_buffer_t_SOURCES = test/buffer.c
test_buffer_t_CPPFLAGS = $(test_cppflags)
test_buffer_t_LDADD = $(test_ldadd)

test_handle_t_SOURCES = test/handle.c
test_handle_t_CPPFLAGS = $(test_cppflags)
test_handle_t_LDADD = $(test_ldadd)

test_msg_handler_t_SOURCES = test/msg_handler.c
test_msg_handler_t_CPPFLAGS = $(test_cppflags)
test_msg_handler_t_LDADD = $(test_ldadd)

test_version_t_SOURCES = test/version.c
test_version_t_CPPFLAGS = $(test_cppflags)
test_version_t_LDADD = $(test_ldadd)

test_rpc_t_SOURCES = test/rpc.c
test_rpc_t_CPPFLAGS = $(test_cppflags)
test_rpc_t_LDADD = $(test_ldadd)

test_rpc_chained_t_SOURCES = test/rpc_chained.c
test_rpc_chained_t_CPPFLAGS = $(test_cppflags)
test_rpc_chained_t_LDADD = $(test_ldadd)

test_dispatch_t_SOURCES = test/dispatch.c
test_dispatch_t_CPPFLAGS = $(test_cppflags)
test_dispatch_t_LDADD = $(test_ldadd)

test_log_t_SOURCES = test/log.c
test_log_t_CPPFLAGS = $(test_cppflags)
test_log_t_LDADD = $(test_ldadd)

test_reactor_loop_t_SOURCES = test/reactor_loop.c
test_reactor_loop_t_CPPFLAGS = $(test_cppflags)
test_reactor_loop_t_LDADD = $(test_ldadd)

test_rpc_security_t_SOURCES = test/rpc_security.c
test_rpc_security_t_CPPFLAGS = $(test_cppflags)
test_rpc_security_t_LDADD = $(test_ldadd)

test_panic_t_SOURCES = test/panic.c
test_panic_t_CPPFLAGS = $(test_cppflags)
test_panic_t_LDADD = $(test_ldadd)

test_attr_t_SOURCES = test/attr.c
test_attr_t_CPPFLAGS = $(test_cppflags)
test_attr_t_LDADD = $(test_ldadd)

test_sync_t_SOURCES = test/sync.c
test_sync_t_CPPFLAGS = $(test_cppflags)
test_sync_t_LDADD = $(test_ldadd)

test_disconnect_t_SOURCES = test/disconnect.c
test_disconnect_t_CPPFLAGS = $(test_cppflags)
test_disconnect_t_LDADD = $(test_ldadd)

test_module_t_SOURCES = test/module.c
test_module_t_CPPFLAGS = $(test_cppflags) \
	-DFAKE1=\"$(abs_builddir)/test/.libs/module_fake1.so\" \
	-DFAKE2=\"$(abs_builddir)/test/.libs/module_fake2.so\"
test_module_t_LDADD = $(test_ldadd)

test_module_fake1_la_SOURCES = test/module_fake1.c
test_module_fake1_la_CPPFLAGS = $(test_cppflags)
test_module_fake1_la_LDFLAGS = $(fluxmod_ldflags) -module -rpath /nowhere

test_module_fake2_la_SOURCES = test/module_fake2.c
test_module_fake2_la_CPPFLAGS = $(test_cppflags)
test_module_fake2_la_LDFLAGS = $(fluxmod_ldflags) -module -rpath /nowhere

test_plugin_t_SOURCES = test/plugin.c
test_plugin_t_CPPFLAGS = $(test_cppflags)
test_plugin_t_LDADD = $(test_ldadd)

test_plugin_foo_la_SOURCES = test/plugin_foo.c
test_plugin_foo_la_CPPFLAGS = $(test_cppflags)
test_plugin_foo_la_LDFLAGS = -module -rpath /nowhere
test_plugin_foo_la_LIBADD = $(test_ldadd)

test_plugin_bar_la_SOURCES = test/plugin_bar.c
test_plugin_bar_la_CPPFLAGS = $(test_cppflags)
test_plugin_bar_la_LDFLAGS = -module -rpath /nowhere
test_plugin_bar_la_LIBADD = $(test_ldadd)
