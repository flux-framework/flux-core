AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(ZMQ_CFLAGS) \
	-Wno-strict-aliasing -Wno-error=strict-aliasing \
	-Wno-parentheses -Wno-error=parentheses

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux

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
	read_all.c \
	read_all.h \
	ev_zmq.c \
	ev_zmq.h \
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
	dirwalk.c \
	tomltk.c \
	tomltk.h \
	timestamp.c \
	timestamp.h \
	cf.c \
	cf.h \
	mnemonic.c \
	mnemonic.h \
	mn_wordlist.c \
	fluid.c \
	fluid.h \
	aux.c \
	aux.h \
	fdutils.c \
	fdutils.h \
	fsd.c \
	fsd.h \
	zsecurity.c \
	zsecurity.h

EXTRA_DIST = veb_mach.c

TESTS = test_ev.t \
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
	test_dirwalk.t \
	test_read_all.t \
	test_tomltk.t \
	test_cf.t \
	test_ipaddr.t \
	test_fluid.t \
	test_aux.t \
	test_fdutils.t \
	test_fsd.t \
	test_zsecurity.t


test_ldadd = \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(top_builddir)/src/common/libev/libev.la \
	$(top_builddir)/src/common/libtomlc99/libtomlc99.la \
	$(ZMQ_LIBS) $(LIBPTHREAD) $(LIBRT) $(JANSSON_LIBS)

test_cppflags = \
	-I$(top_srcdir)/src/common/libtap \
	$(AM_CPPFLAGS) $(JANSSON_CFLAGS)

check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
       $(top_srcdir)/config/tap-driver.sh

test_ev_t_SOURCES = test/ev.c
test_ev_t_CPPFLAGS = $(test_cppflags)
test_ev_t_LDADD = $(test_ldadd)

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
test_cronodate_t_LDADD = \
	$(builddir)/cronodate.lo \
	$(top_builddir)/src/common/libidset/libidset.la \
	$(builddir)/veb.lo \
	$(builddir)/xzmalloc.lo \
	$(top_builddir)/src/common/libtap/libtap.la

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

test_read_all_t_SOURCES = test/read_all.c
test_read_all_t_CPPFLAGS = $(test_cppflags)
test_read_all_t_LDADD = $(test_ldadd)

test_tomltk_t_SOURCES = test/tomltk.c
test_tomltk_t_CPPFLAGS = $(test_cppflags)
test_tomltk_t_LDADD = $(test_ldadd)

test_cf_t_SOURCES = test/cf.c
test_cf_t_CPPFLAGS = $(test_cppflags)
test_cf_t_LDADD = $(test_ldadd)

test_ipaddr_t_SOURCES = test/ipaddr.c
test_ipaddr_t_CPPFLAGS = $(test_cppflags)
test_ipaddr_t_LDADD = $(test_ldadd)

test_fluid_t_SOURCES = test/fluid.c
test_fluid_t_CPPFLAGS = $(test_cppflags)
test_fluid_t_LDADD = $(test_ldadd)

test_aux_t_SOURCES = test/aux.c
test_aux_t_CPPFLAGS = $(test_cppflags)
test_aux_t_LDADD = $(test_ldadd)

test_fdutils_t_SOURCES = test/fdutils.c
test_fdutils_t_CPPFLAGS = $(test_cppflags)
test_fdutils_t_LDADD = $(test_ldadd)

test_fsd_t_SOURCES = test/fsd.c
test_fsd_t_CPPFLAGS = $(test_cppflags)
test_fsd_t_LDADD = $(test_ldadd)

test_zsecurity_t_SOURCES = test/zsecurity.c
test_zsecurity_t_CPPFLAGS = $(test_cppflags)
test_zsecurity_t_LDADD = $(test_ldadd)
