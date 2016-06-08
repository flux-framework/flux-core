AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS) \
	-Wno-strict-aliasing -Wno-error=strict-aliasing

noinst_LTLIBRARIES = libutil.la

libutil_la_SOURCES = \
	ipaddr.c \
	ipaddr.h \
	log.c \
	log.h \
	xzmalloc.c \
	xzmalloc.h \
	base64_json.c \
	base64_json.h \
	getrusage_json.c \
	getrusage_json.h \
	monotime.c \
	monotime.h \
	setenvf.c \
	setenvf.h \
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
	iterators.h \
	macros.h \
	sha1.h \
	sha1.c \
	shastring.h \
	shastring.c \
	fdwalk.h \
	fdwalk.c \
	popen2.h \
	popen2.c \
	environment.h \
	environment.c \
	kary.h \
	kary.c

EXTRA_DIST = veb_mach.c

TESTS = test_nodeset.t \
	test_optparse.t \
	test_ev.t \
	test_coproc.t \
	test_base64.t \
	test_encode.t \
	test_msglist.t \
	test_sha1.t \
	test_popen2.t \
	test_kary.t

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
test_coproc_t_CFLAGS = $(AM_CFLAGS) -fno-builtin
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

test_sha1_t_SOURCES = test/sha1.c
test_sha1_t_CPPFLAGS = $(test_cppflags)
test_sha1_t_LDADD = $(test_ldadd)

test_popen2_t_SOURCES = test/popen2.c
test_popen2_t_CPPFLAGS = $(test_cppflags)
test_popen2_t_LDADD = $(test_ldadd)

test_kary_t_SOURCES = test/kary.c
test_kary_t_CPPFLAGS = $(test_cppflags)
test_kary_t_LDADD = $(test_ldadd)
