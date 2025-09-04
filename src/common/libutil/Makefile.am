AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	-Wno-strict-aliasing -Wno-error=strict-aliasing \
	-Wno-parentheses -Wno-error=parentheses

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	$(CODE_COVERAGE_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_srcdir)/src/common/libccan \
	-I$(top_builddir)/src/common/libflux \
	-DABS_TOP_BUILDDIR=\"$(abs_top_builddir)\" \
	$(JANSSON_CFLAGS)

noinst_LTLIBRARIES = libutil.la

libutil_la_SOURCES = \
	ipaddr.c \
	ipaddr.h \
	cidr.c \
	cidr.h \
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
	read_all.c \
	read_all.h \
	cleanup.c \
	cleanup.h \
	unlink_recursive.c \
	unlink_recursive.h \
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
	errno_safe.h \
	intree.c \
	intree.h \
	llog.h \
	grudgeset.c \
	grudgeset.h \
	jpath.c \
	jpath.h \
	uri.c \
	uri.h \
	errprintf.c \
	errprintf.h \
	hola.c \
	hola.h \
	slice.c \
	slice.h \
	strstrip.c \
	strstrip.h \
	basemoji.h \
	basemoji.c \
	sigutil.h \
	sigutil.c \
	parse_size.h \
	parse_size.c \
	basename.h \
	basename.c \
	ansi_color.h \
	cgroup.h \
	cgroup.c

libutil_la_LDFLAGS = \
        $(AM_LDFLAGS) \
        $(APPLE_CORESERVICES_FRAMEWORK) \
        $(APPLE_SYSTEMCONFIGURATION_FRAMEWORK)

TESTS = test_sha1.t \
	test_sha256.t \
	test_popen2.t \
	test_kary.t \
	test_cronodate.t \
	test_wallclock.t \
	test_stdlog.t \
	test_lru_cache.t \
	test_unlink.t \
	test_cleanup.t \
	test_blobref.t \
	test_dirwalk.t \
	test_read_all.t \
	test_tomltk.t \
	test_ipaddr.t \
	test_cidr.t \
	test_fluid.t \
	test_aux.t \
	test_fdutils.t \
	test_fsd.t \
	test_intree.t \
	test_fdwalk.t \
	test_grudgeset.t \
	test_jpath.t \
	test_errprintf.t \
	test_hola.t \
	test_strstrip.t \
	test_slice.t \
	test_timestamp.t \
	test_environment.t \
	test_basemoji.t \
	test_sigutil.t \
	test_parse_size.t

test_ldadd = \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(top_builddir)/src/common/libtomlc99/libtomlc99.la \
	$(top_builddir)/src/common/libczmqcontainers/libczmqcontainers.la \
	$(top_builddir)/src/common/libccan/libccan.la \
	$(top_builddir)/src/common/libmissing/libmissing.la \
	$(LIBPTHREAD) \
	$(LIBRT) \
	$(JANSSON_LIBS)

test_cppflags = \
	-I$(top_srcdir)/src/common/libtap \
	$(AM_CPPFLAGS) $(JANSSON_CFLAGS)

check_PROGRAMS = \
	$(TESTS) \
	test_getaddr

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
       $(top_srcdir)/config/tap-driver.sh

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
	$(top_builddir)/src/common/libtap/libtap.la

test_wallclock_t_SOURCES = test/wallclock.c
test_wallclock_t_CPPFLAGS = $(test_cppflags)
test_wallclock_t_LDADD = $(test_ldadd)

test_stdlog_t_SOURCES = test/stdlog.c
test_stdlog_t_CPPFLAGS = $(test_cppflags)
test_stdlog_t_LDADD = $(test_ldadd)

test_lru_cache_t_SOURCES = test/lru_cache.c
test_lru_cache_t_CPPFLAGS = $(test_cppflags)
test_lru_cache_t_LDADD = $(test_ldadd)

test_blobref_t_SOURCES = test/blobref.c
test_blobref_t_CPPFLAGS = $(test_cppflags)
test_blobref_t_LDADD = $(test_ldadd)

test_unlink_t_SOURCES = test/unlink.c
test_unlink_t_CPPFLAGS = $(test_cppflags)
test_unlink_t_LDADD = $(test_ldadd)

test_cleanup_t_SOURCES = test/cleanup.c
test_cleanup_t_CPPFLAGS = $(test_cppflags)
test_cleanup_t_LDADD = $(test_ldadd)

test_dirwalk_t_SOURCES = test/dirwalk.c
test_dirwalk_t_CPPFLAGS = $(test_cppflags)
test_dirwalk_t_LDADD = $(test_ldadd)

test_read_all_t_SOURCES = test/read_all.c
test_read_all_t_CPPFLAGS = $(test_cppflags)
test_read_all_t_LDADD = $(test_ldadd)

test_tomltk_t_SOURCES = test/tomltk.c
test_tomltk_t_CPPFLAGS = $(test_cppflags)
test_tomltk_t_LDADD = $(test_ldadd)

test_ipaddr_t_SOURCES = test/ipaddr.c
test_ipaddr_t_CPPFLAGS = $(test_cppflags)
test_ipaddr_t_LDADD = $(test_ldadd)

test_getaddr_SOURCES = test/getaddr.c
test_getaddr_CPPFLAGS = $(test_cppflags)
test_getaddr_LDADD = $(test_ldadd)

test_cidr_t_SOURCES = test/cidr.c
test_cidr_t_CPPFLAGS = $(test_cppflags)
test_cidr_t_LDADD = $(test_ldadd)

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

test_intree_t_SOURCES = test/intree.c
test_intree_t_CPPFLAGS = $(test_cppflags)
test_intree_t_LDADD = $(test_ldadd)

test_fdwalk_t_SOURCES = test/fdwalk.c
test_fdwalk_t_CPPFLAGS = $(test_cppflags)
test_fdwalk_t_LDADD = $(test_ldadd)

test_grudgeset_t_SOURCES = test/grudgeset.c
test_grudgeset_t_CPPFLAGS = $(test_cppflags)
test_grudgeset_t_LDADD = $(test_ldadd)

test_jpath_t_SOURCES = test/jpath.c
test_jpath_t_CPPFLAGS = $(test_cppflags)
test_jpath_t_LDADD = $(test_ldadd)

test_errprintf_t_SOURCES = test/errprintf.c
test_errprintf_t_CPPFLAGS = $(test_cppflags)
test_errprintf_t_LDADD = $(test_ldadd)

test_strstrip_t_SOURCES = test/strstrip.c
test_strstrip_t_CPPFLAGS = $(test_cppflags)
test_strstrip_t_LDADD = $(test_ldadd)

test_hola_t_SOURCES = test/hola.c
test_hola_t_CPPFLAGS = $(test_cppflags)
test_hola_t_LDADD = $(test_ldadd)

test_slice_t_SOURCES = test/slice.c
test_slice_t_CPPFLAGS = $(test_cppflags)
test_slice_t_LDADD = $(test_ldadd)

test_timestamp_t_SOURCES = test/timestamp.c
test_timestamp_t_CPPFLAGS = $(test_cppflags)
test_timestamp_t_LDADD = $(test_ldadd)

test_environment_t_SOURCES = test/environment.c
test_environment_t_CPPFLAGS = $(test_cppflags)
test_environment_t_LDADD = $(test_ldadd)

test_basemoji_t_SOURCES = test/basemoji.c
test_basemoji_t_CPPFLAGS = $(test_cppflags)
test_basemoji_t_LDADD = $(test_ldadd)

test_sigutil_t_SOURCES = test/sigutil.c
test_sigutil_t_CPPFLAGS = $(test_cppflags)
test_sigutil_t_LDADD = $(test_ldadd)

test_parse_size_t_SOURCES = test/parse_size.c
test_parse_size_t_CPPFLAGS = $(test_cppflags)
test_parse_size_t_LDADD = $(test_ldadd)
