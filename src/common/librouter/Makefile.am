AM_CFLAGS = \
        $(WARNING_CFLAGS) \
        $(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
        $(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	$(CODE_COVERAGE_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux \
	-I$(top_srcdir)/src/common/libccan \
	$(LIBUUID_CFLAGS)

noinst_LTLIBRARIES = \
	librouter.la

librouter_la_SOURCES = \
	sendfd.h \
	sendfd.c \
	auth.c \
	auth.h \
	usock.c \
	usock.h \
	disconnect.h \
	disconnect.c \
	subhash.h \
	subhash.c \
	servhash.h \
	servhash.c \
	router.h \
	router.c \
	usock_service.h \
	usock_service.c \
	msg_hash.h \
	msg_hash.c \
	rpc_track.h \
	rpc_track.c

TESTS = \
	test_sendfd.t \
        test_disconnect.t \
	test_auth.t \
	test_usock.t \
	test_usock_echo.t \
	test_usock_epipe.t \
	test_usock_emfile.t \
	test_subhash.t \
	test_router.t \
	test_servhash.t \
	test_usock_service.t \
	test_msg_hash.t \
	test_rpc_track.t

check_PROGRAMS = \
        $(TESTS)

check_LTLIBRARIES = libtestutil.la

libtestutil_la_SOURCES = \
        test/usock_util.c \
        test/usock_util.h

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
        $(top_srcdir)/config/tap-driver.sh

test_ldadd = \
	$(builddir)/libtestutil.la \
        $(top_builddir)/src/common/librouter/librouter.la \
        $(top_builddir)/src/common/libtestutil/libtestutil.la \
        $(top_builddir)/src/common/libflux-core.la \
        $(top_builddir)/src/common/libflux-internal.la \
        $(top_builddir)/src/common/libtap/libtap.la

test_cppflags = \
        $(AM_CPPFLAGS) \
        -I$(top_srcdir)/src/common/libtap

test_ldflags = \
	-no-install

test_sendfd_t_SOURCES = test/sendfd.c
test_sendfd_t_CPPFLAGS = $(test_cppflags)
test_sendfd_t_LDADD = $(test_ldadd)
test_sendfd_t_LDFLAGS = $(test_ldflags)

test_disconnect_t_SOURCES = test/disconnect.c
test_disconnect_t_CPPFLAGS = $(test_cppflags)
test_disconnect_t_LDADD = $(test_ldadd)
test_disconnect_t_LDFLAGS = $(test_ldflags)

test_auth_t_SOURCES = test/auth.c
test_auth_t_CPPFLAGS = $(test_cppflags)
test_auth_t_LDADD = $(test_ldadd)
test_auth_t_LDFLAGS = $(test_ldflags)

test_usock_t_SOURCES = test/usock.c
test_usock_t_CPPFLAGS = $(test_cppflags)
test_usock_t_LDADD = $(test_ldadd)
test_usock_t_LDFLAGS = $(test_ldflags)

test_usock_echo_t_SOURCES = test/usock_echo.c
test_usock_echo_t_CPPFLAGS = $(test_cppflags)
test_usock_echo_t_LDADD = $(test_ldadd)
test_usock_echo_t_LDFLAGS = $(test_ldflags)

test_usock_epipe_t_SOURCES = test/usock_epipe.c
test_usock_epipe_t_CPPFLAGS = $(test_cppflags)
test_usock_epipe_t_LDADD = $(test_ldadd)
test_usock_epipe_t_LDFLAGS = $(test_ldflags)

test_usock_emfile_t_SOURCES = test/usock_emfile.c
test_usock_emfile_t_CPPFLAGS = $(test_cppflags)
test_usock_emfile_t_LDADD = $(test_ldadd)
test_usock_emfile_t_LDFLAGS = $(test_ldflags)

test_subhash_t_SOURCES = test/subhash.c
test_subhash_t_CPPFLAGS = $(test_cppflags)
test_subhash_t_LDADD = $(test_ldadd)
test_subhash_t_LDFLAGS = $(test_ldflags)

test_router_t_SOURCES = test/router.c
test_router_t_CPPFLAGS = $(test_cppflags)
test_router_t_LDADD = $(test_ldadd)
test_router_t_LDFLAGS = $(test_ldflags)

test_servhash_t_SOURCES = test/servhash.c
test_servhash_t_CPPFLAGS = $(test_cppflags)
test_servhash_t_LDADD = $(test_ldadd)
test_servhash_t_LDFLAGS = $(test_ldflags)

test_usock_service_t_SOURCES = test/usock_service.c
test_usock_service_t_CPPFLAGS = $(test_cppflags)
test_usock_service_t_LDADD = $(test_ldadd)
test_usock_service_t_LDFLAGS = $(test_ldflags)

test_msg_hash_t_SOURCES = test/msg_hash.c
test_msg_hash_t_CPPFLAGS = $(test_cppflags)
test_msg_hash_t_LDADD = $(test_ldadd)
test_msg_hash_t_LDFLAGS = $(test_ldflags)

test_rpc_track_t_SOURCES = test/rpc_track.c
test_rpc_track_t_CPPFLAGS = $(test_cppflags)
test_rpc_track_t_LDADD = $(test_ldadd)
test_rpc_track_t_LDFLAGS = $(test_ldflags)
