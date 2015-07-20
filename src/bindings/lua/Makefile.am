AM_CFLAGS =	@GCCWARN@
AM_CPPFLAGS =	-I$(top_srcdir) -I$(top_srcdir)/src/include \
		$(LUA_INCLUDE) $(JSON_CFLAGS) $(ZMQ_CFLAGS)

fluxluadir = $(luadir)/flux-lua

dist_lua_SCRIPTS = \
	wreck.lua

dist_fluxlua_SCRIPTS = \
	flux-lua/timer.lua \
	flux-lua/alt_getopt.lua \
	flux-lua/posix.lua \
	flux-lua/base64.lua

luaexec_LTLIBRARIES = \
	flux.la

check_LTLIBRARIES = \
	tests/jsontest.la \
	tests/zmsgtest.la

noinst_LTLIBRARIES = \
	libfluxlua.la \
	lalarm.la

luamod_ldflags = \
	-avoid-version -module -shared --disable-static \
	-Wl,--no-undefined

luamod_libadd = \
	$(top_builddir)/src/modules/kvs/libkvs.la \
	$(top_builddir)/src/modules/libmrpc/libmrpc.la \
	$(top_builddir)/src/modules/libkz/libkz.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(LUA_LIB) $(JSON_LIBS) $(ZMQ_LIBS)

flux_la_LDFLAGS = \
	$(luamod_ldflags)

flux_la_LIBADD = \
	$(luamod_libadd)

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

lalarm_la_SOURCES = \
	lalarm.c
lalarm_la_LDFLAGS = \
	$(luamod_ldflags) -rpath /dev/null

# lalarm doesn't need any of the other flux libs
lalarm_la_LIBADD = \
	$(LUA_LIB)

tests_jsontest_la_LDFLAGS = \
	$(luamod_ldflags) -rpath /dev/null

tests_jsontest_la_LIBADD = \
	$(luamod_libadd)

tests_jsontest_la_SOURCES = \
	lutil.c \
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
	$(luamod_libadd)

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
