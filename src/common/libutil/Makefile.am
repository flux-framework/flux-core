AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	$(ZMQ_CFLAGS) \
	-Wno-strict-aliasing -Wno-error=strict-aliasing

noinst_LTLIBRARIES = libutil.la

libutil_la_SOURCES = \
	ipaddr.c \
	ipaddr.h \
	log.c \
	log.h \
	xzmalloc.c \
	xzmalloc.h \
	monotime.c \
	monotime.h \
	setenvf.c \
	setenvf.h \
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
	base64.c \
	base64.h \
	msglist.c \
	msglist.h \
	cleanup.c \
	cleanup.h \
	unlink_recursive.c \
	unlink_recursive.h \
	sds.c \
	sds.h \
	sdsalloc.h \
	iterators.h \
	macros.h \
	sha1.h \
	sha1.c \
	blobref.h \
	blobref.c \
	sha256.h \
	sha256.c \
	fdwalk.h \
	fdwalk.c \
	popen2.h \
	popen2.c \
	environment.h \
	environment.c \
	kary.h \
	kary.c \
	cronodate.h \
	cronodate.c \
	wallclock.h \
	wallclock.c \
	stdlog.h \
	stdlog.c \
	oom.h \
	lru_cache.h \
	lru_cache.c \
	dirwalk.h \
	dirwalk.c

EXTRA_DIST = veb_mach.c

TESTS = test_nodeset.t \
	test_ev.t \
	test_base64.t \
	test_msglist.t \
	test_sha1.t \
	test_sha256.t \
	test_popen2.t \
	test_kary.t \
	test_cronodate.t \
	test_wallclock.t \
	test_stdlog.t \
	test_veb.t \
	test_lru_cache.t \
	test_unlink.t \
	test_cleanup.t \
	test_blobref.t \
	test_dirwalk.t

test_ldadd = \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(top_builddir)/src/common/libev/libev.la \
	$(top_builddir)/src/common/libjson-c/libjson-c.la \
	$(ZMQ_LIBS) $(LIBPTHREAD) $(LIBRT)

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

test_ev_t_SOURCES = test/ev.c
test_ev_t_CPPFLAGS = $(test_cppflags)
test_ev_t_LDADD = $(test_ldadd)

test_base64_t_SOURCES = test/base64_test.c
test_base64_t_CPPFLAGS = $(test_cppflags)
test_base64_t_LDADD = $(test_ldadd)

test_msglist_t_SOURCES = test/msglist.c
test_msglist_t_CPPFLAGS = $(test_cppflags)
test_msglist_t_LDADD = $(test_ldadd)

test_sha1_t_SOURCES = test/sha1.c
test_sha1_t_CPPFLAGS = $(test_cppflags)
test_sha1_t_LDADD = $(test_ldadd)

test_sha256_t_SOURCES = test/sha256.c
test_sha256_t_CPPFLAGS = $(test_cppflags)
test_sha256_t_LDADD = $(test_ldadd)

test_popen2_t_SOURCES = test/popen2.c
test_popen2_t_CPPFLAGS = $(test_cppflags)
test_popen2_t_LDADD = $(test_ldadd)

test_kary_t_SOURCES = test/kary.c
test_kary_t_CPPFLAGS = $(test_cppflags)
test_kary_t_LDADD = $(test_ldadd)

test_cronodate_t_SOURCES = test/cronodate.c
test_cronodate_t_CPPFLAGS = $(test_cppflags)
test_cronodate_t_LDADD = $(test_ldadd)

test_wallclock_t_SOURCES = test/wallclock.c
test_wallclock_t_CPPFLAGS = $(test_cppflags)
test_wallclock_t_LDADD = $(test_ldadd)

test_stdlog_t_SOURCES = test/stdlog.c
test_stdlog_t_CPPFLAGS = $(test_cppflags)
test_stdlog_t_LDADD = $(test_ldadd)

test_veb_t_SOURCES = test/veb.c
test_veb_t_CPPFLAGS = $(test_cppflags)
test_veb_t_LDADD = $(test_ldadd)

test_lru_cache_t_SOURCES = test/lru_cache.c
test_lru_cache_t_CPPFLAGS = $(test_cppflags)
test_lru_cache_t_LDADD = $(test_ldadd)

test_blobref_t_SOURCES = test/blobref.c
test_blobref_t_CPPFLAGS = $(test_cppflags) $(JANSSON_CFLAGS)
test_blobref_t_LDADD = $(test_ldadd) $(JANSSON_LIBS)

test_unlink_t_SOURCES = test/unlink.c
test_unlink_t_CPPFLAGS = $(test_cppflags) $(JANSSON_CFLAGS)
test_unlink_t_LDADD = $(test_ldadd) $(JANSSON_LIBS)

test_cleanup_t_SOURCES = test/cleanup.c
test_cleanup_t_CPPFLAGS = $(test_cppflags) $(JANSSON_CFLAGS)
test_cleanup_t_LDADD = $(test_ldadd) $(JANSSON_LIBS)

test_dirwalk_t_SOURCES = test/dirwalk.c
test_dirwalk_t_CPPFLAGS = $(test_cppflags)
test_dirwalk_t_LDADD = $(test_ldadd)
