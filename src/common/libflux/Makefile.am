AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	$(CODE_COVERAGE_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_srcdir)/src/common/libccan \
	-I$(top_builddir) \
	-I$(top_builddir)/src/common/libflux \
	-DABS_TOP_BUILDDIR=\"${abs_top_builddir}\" \
	-DABS_TOP_SRCDIR=\"${abs_top_srcdir}\" \
	-DLUADIR=\"$(luadir)\" \
	-DLUAEXECDIR=\"$(luaexecdir)\" \
	$(JANSSON_CFLAGS) \
	$(LIBUUID_CFLAGS) \
	$(LIBUV_CFLAGS)

fluxcoreinclude_HEADERS = \
	flux.h \
	types.h \
	handle.h \
	connector.h \
	reactor.h \
	watcher.h \
	msg_handler.h \
	message.h \
	msglist.h \
	request.h \
	control.h \
	response.h \
	rpc.h \
	event.h \
	module.h \
	attr.h \
	flog.h \
	conf.h \
	future.h \
	barrier.h \
	service.h \
	plugin.h \
	sync.h \
	disconnect.h \
	stats.h

nodist_fluxcoreinclude_HEADERS = \
	version.h

noinst_LTLIBRARIES = \
	libflux.la

libflux_la_SOURCES = \
	flog.c \
	attr.c \
	handle.c \
	msg_deque.c \
	msg_deque.h \
	connector_loop.c \
	connector_interthread.c \
	connector_local.c \
	reactor.c \
	reactor_private.h \
	watcher.c \
	watcher_private.h \
	hwatcher.c \
	msg_handler.c \
	message.c \
	message_private.h \
	message_iovec.h \
	message_iovec.c \
	message_route.h \
	message_route.c \
	message_proto.h \
	message_proto.c \
	msglist.c \
	request.c \
	response.c \
	rpc.c \
	event.c \
	module.c \
	conf_private.h \
	conf.c \
	control.c \
	future.c \
	composite_future.c \
	barrier.c \
	service.c \
	version.c \
	plugin.c \
	plugin_private.h \
	sync.c \
	disconnect.c \
	stats.c \
	fripp.h \
	fripp.c

if LIBUV
else
libflux_la_SOURCES += watcher_ev.c
endif

libflux_la_LDFLAGS = \
	$(AM_LDFLAGS)

TESTS = test_message.t \
	test_msglist.t \
	test_interthread.t \
	test_request.t \
	test_response.t \
	test_event.t \
	test_future.t \
	test_composite_future.t \
	test_reactor.t \
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
	test_attr.t \
	test_module.t \
	test_plugin.t \
	test_sync.t \
	test_disconnect.t \
	test_msg_deque.t \
	test_rpcscale.t

test_ldadd = \
	$(top_builddir)/src/common/libtestutil/libtestutil.la \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libkvs/libkvs.la \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(LIBUUID_LIBS) \
	$(JANSSON_LIBS) \
	$(LIBPTHREAD) \
	$(LIBDL)

test_cppflags = \
        -I$(top_srcdir)/src/common/libtap \
        $(AM_CPPFLAGS)

check_LTLIBRARIES = \
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

test_rpcscale_t_SOURCES = test/rpcscale.c
test_rpcscale_t_CPPFLAGS = $(test_cppflags)
test_rpcscale_t_LDADD = \
	$(top_builddir)/src/common/liboptparse/liboptparse.la \
	$(test_ldadd)

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

test_attr_t_SOURCES = test/attr.c
test_attr_t_CPPFLAGS = $(test_cppflags)
test_attr_t_LDADD = $(test_ldadd)

test_sync_t_SOURCES = test/sync.c
test_sync_t_CPPFLAGS = $(test_cppflags)
test_sync_t_LDADD = $(test_ldadd)

test_disconnect_t_SOURCES = test/disconnect.c
test_disconnect_t_CPPFLAGS = $(test_cppflags)
test_disconnect_t_LDADD = $(test_ldadd)

test_interthread_t_SOURCES = test/interthread.c
test_interthread_t_CPPFLAGS = $(test_cppflags)
test_interthread_t_LDADD = $(test_ldadd)

test_msg_deque_t_SOURCES = test/msg_deque.c
test_msg_deque_t_CPPFLAGS = $(test_cppflags)
test_msg_deque_t_LDADD = $(test_ldadd)

test_module_t_SOURCES = test/module.c
test_module_t_CPPFLAGS = $(test_cppflags)
test_module_t_LDADD = $(test_ldadd)

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
