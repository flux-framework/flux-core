AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS) \
	-Wno-strict-aliasing \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

noinst_LTLIBRARIES = libutil.la

libutil_la_SOURCES = \
	ipaddr.c \
	ipaddr.h \
	log.c \
	log.h \
	xzmalloc.c \
	xzmalloc.h \
	jsonutil.c \
	jsonutil.h \
	monotime.c \
	monotime.h \
	setenvf.c \
	setenvf.h \
	env.c \
	env.h \
	optparse.c \
	optparse.h \
	tstat.c \
	tstat.h \
	veb.c \
	veb.h \
	nodeset.c \
	nodeset.h \
	shortjson.h \
	readall.c \
	readall.h \
	ev_zmq.c \
	ev_zmq.h \
	ev_zlist.c \
	ev_zlist.h \
	coproc.c \
	coproc.h \
	base64.c \
	base64.h \
	msglist.c \
	msglist.h \
	cleanup.c \
	cleanup.h \
	sds.c \
	sds.h \
	sdsalloc.h \
	vec.c \
	vec.h \
	iterators.h

EXTRA_DIST = veb_mach.c

TESTS = test_nodeset.t \
	test_optparse.t \
	test_ev.t \
	test_coproc.t \
	test_base64.t \
	test_encode.t \
	test_msglist.t

test_ldadd = \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(top_builddir)/src/common/libev/libev.la \
	$(JSON_LIBS) $(ZMQ_LIBS) $(LIBPTHREAD)

test_cppflags = \
	-I$(top_srcdir)/src/common/libtap \
	$(AM_CPPFLAGS)

check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
       $(top_srcdir)/config/tap-driver.sh

test_nodeset_t_SOURCES = test/nodeset.c
test_nodeset_t_CPPFLAGS = $(test_cppflags)
test_nodeset_t_LDADD = $(test_ldadd)

test_optparse_t_SOURCES = test/optparse.c
test_optparse_t_CPPFLAGS = $(test_cppflags)
test_optparse_t_LDADD = $(test_ldadd)

test_ev_t_SOURCES = test/ev.c
test_ev_t_CPPFLAGS = $(test_cppflags)
test_ev_t_LDADD = $(test_ldadd)

test_coproc_t_SOURCES = test/coproc.c
test_coproc_t_CPPFLAGS = $(test_cppflags)
test_coproc_t_LDADD = $(test_ldadd)

test_base64_t_SOURCES = test/base64_test.c
test_base64_t_CPPFLAGS = $(test_cppflags)
test_base64_t_LDADD = $(test_ldadd)

test_encode_t_SOURCES = test/encode.c
test_encode_t_CPPFLAGS = $(test_cppflags)
test_encode_t_LDADD = $(test_ldadd)

test_msglist_t_SOURCES = test/msglist.c
test_msglist_t_CPPFLAGS = $(test_cppflags)
test_msglist_t_LDADD = $(test_ldadd)
