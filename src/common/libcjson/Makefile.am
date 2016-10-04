AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

noinst_LTLIBRARIES = libcjson.la

libcjson_la_SOURCES = \
	cJSON.c \
	cJSON_Utils.c \
	cJSON.h \
	cJSON_Utils.h

EXTRA_DIST = LICENSE \
	tests/test1 \
	tests/test2 \
	tests/test3 \
	tests/test4 \
	tests/test5 \
	tests/test6

TESTS = test_cjson.t

check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	               $(top_srcdir)/config/tap-driver.sh

test_cjson_t_SOURCES = test.c
test_cjson_t_CPPFLAGS = $(AM_CPPFLAGS) \
			-DSRCDIR=\"$(abs_top_srcdir)/src/common/libcjson\"
test_cjson_t_LDADD = $(top_builddir)/src/common/libcjson/libcjson.la \
                     $(top_builddir)/src/common/libtap/libtap.la \
		     $(top_builddir)/src/common/libutil/libutil.la
