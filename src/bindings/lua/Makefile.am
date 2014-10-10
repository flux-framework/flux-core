AM_CFLAGS =	@GCCWARN@
AM_CPPFLAGS =	-I$(top_srcdir) -I$(top_srcdir)/src/include \
		$(LUA_INCLUDE) $(JSON_CFLAGS)

fluxluadir = $(luadir)/flux-lua

dist_lua_SCRIPTS = \
	wreck.lua

dist_fluxlua_SCRIPTS = \
	flux-lua/timer.lua \
	flux-lua/alt_getopt.lua \
	flux-lua/posix.lua

luaexec_LTLIBRARIES = \
	flux.la

check_LTLIBRARIES = \
	tests/jsontest.la \
	tests/zmsgtest.la

noinst_LTLIBRARIES = \
	libfluxlua.la

luamod_ldflags = \
	-avoid-version -module

flux_la_LDFLAGS = \
	$(luamod_ldflags)

flux_la_LIBADD = \
	$(top_builddir)/src/common/libmrpc/libmrpc.la \
	$(top_builddir)/src/common/libzio/libzio.la \
	$(top_builddir)/src/lib/libcore/libflux-core.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(JSON_LIBS) \
	$(LIBZMQ) \
	$(LIBCZMQ) \
	$(LIBUTIL)

flux_la_SOURCES = \
	flux-lua.c \
	flux-lua.h \
	kvs-lua.c \
	kvs-lua.h \
	json-lua.c \
	json-lua.h \
	zmsg-lua.c \
	zmsg-lua.h \
	lutil.c \
	lutil.h

libfluxlua_la_SOURCES = \
	flux-lua.c \
	kvs-lua.c \
	json-lua.c \
	zmsg-lua.c \
	lutil.c

tests_jsontest_la_LDFLAGS = \
	$(luamod_ldflags) -rpath /dev/null

tests_jsontest_la_LIBADD = \
	$(JSON_LIBS) \
	$(LUA_LIB)

tests_jsontest_la_SOURCES = \
	json-lua.c \
	tests/json-test.c

tests_zmsgtest_la_LDFLAGS = \
	$(luamod_ldflags) -rpath /dev/null

tests_zmsgtest_la_SOURCES = \
	lutil.c \
	json-lua.c \
	zmsg-lua.c \
	tests/zmsg-test.c

tests_zmsgtest_la_LIBADD = \
	$(top_builddir)/src/lib/libcore/libflux-core.la \
	$(LIBCZMQ) \
	$(LUA_LIB)

TESTS = \
	tests/t0000-json.t \
	tests/t0001-zmsg.t

EXTRA_DIST = \
	README \
	$(TESTS) \
	Test

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh
TESTS_ENVIRONMENT = \
	LUA_PATH="$(abs_srcdir)/?.lua;;" \
	LUA_CPATH="$(abs_builddir)/tests/.libs/?.so;;;"
