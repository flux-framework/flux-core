AM_CFLAGS =	$(WARNING_CFLAGS) $(CODE_COVERAGE_CFLAGS)
AM_LDFLAGS =	$(CODE_COVERAGE_LDFLAGS)
AM_CPPFLAGS =	-I$(top_srcdir) -I$(top_srcdir)/src/include \
		$(LUA_INCLUDE) $(ZMQ_CFLAGS)

fluxluadir =     $(luadir)/flux
fluxometerdir =  $(luadir)/fluxometer
wreckluadir =    $(luadir)/wreck
lustachedir =    $(fluxluadir)/lustache
fluxluaexecdir = $(luaexecdir)/flux

dist_lua_SCRIPTS = \
	wreck.lua

nobase_dist_lua_SCRIPTS = \
	flux/timer.lua \
	flux/alt_getopt.lua \
	flux/posix.lua \
	flux/base64.lua \
	flux/lustache.lua \
	flux/Subcommander.lua \
	wreck/io.lua

#  Required because install(1) doesn't seem to preserve
#   full path when copying to directory:
dist_lustache_SCRIPTS = \
	flux/lustache/context.lua \
	flux/lustache/renderer.lua \
	flux/lustache/scanner.lua

nobase_dist_fluxometer_SCRIPTS = \
	Test/Builder/NoOutput.lua \
	Test/Builder/SocketOutput.lua \
	Test/Builder/Tester.lua \
	Test/Builder/Tester/File.lua \
	Test/Builder.lua \
	Test/More.lua

luaexec_LTLIBRARIES = \
	flux.la

fluxluaexec_LTLIBRARIES = \
	flux/affinity.la \
	flux/hostlist.la

check_LTLIBRARIES = \
	tests/jsontest.la \
	tests/zmsgtest.la

noinst_LTLIBRARIES = \
	libfluxlua.la \
	lalarm.la

luamod_ldflags = \
	-avoid-version -module -shared --disable-static \
	$(san_ld_zdef_flag)

luamod_libadd = \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(LUA_LIB) $(ZMQ_LIBS)

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

flux_affinity_la_LDFLAGS = \
	$(luamod_ldflags) \
	-Wl,--defsym=luaopen_flux_affinity=luaopen_affinity

flux_affinity_la_LIBADD = \
	$(LUA_LIB)

flux_affinity_la_SOURCES = \
	lua-affinity/lua-affinity.c \
	lua-affinity/cpuset-str.h \
	lua-affinity/cpuset-str.c

flux_hostlist_la_LDFLAGS = \
	$(luamod_ldflags) \
	-Wl,--defsym=luaopen_flux_hostlist=luaopen_hostlist

flux_hostlist_la_LIBADD = \
	$(LUA_LIB)

flux_hostlist_la_SOURCES = \
	lua-hostlist/hostlist.c \
	lua-hostlist/hostlist.h \
	lua-hostlist/lua-hostlist.c

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
	tests/t0001-zmsg.t \
	lua-affinity/tests/test.lua \
	lua-hostlist/tests/runtest.sh

EXTRA_DIST = \
	README \
	$(TESTS) \
	Test/COPYRIGHT \
	Test/README.md \
	Test/dist.info \
	lua-affinity/COPYING \
	lua-affinity/DISCLAIMER.LLNS \
	lua-affinity/README.md \
	lua-hostlist/COPYING \
	lua-hostlist/DISCLAIMER \
	lua-hostlist/META \
	lua-hostlist/NEWS \
	lua-hostlist/README.md \
	lua-hostlist/test.lua \
	lua-hostlist/tests/lunit \
	lua-hostlist/tests/lunit-console.lua \
	lua-hostlist/tests/lunit.lua \
	lua-hostlist/tests/tests.lua

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh
TESTS_ENVIRONMENT = \
	LUA_PATH="$(abs_srcdir)/?.lua;;;" \
	LUA_CPATH="$(abs_builddir)/tests/.libs/?.so;$(abs_builddir)/flux/?.so;;;"


# Copy any Lua modules in flux/.libs to ${top_builddir}/flux/*.so so that
#  they can be used as require 'flux.<name>' in-tree
#
.PHONY: convenience-link clean-convenience-link

convenience-link: $(luaexec_LTLIBRARIES) $(fluxluaexec_LTLIBRARIES)
	@for f in $^; do \
	  soname=`$(GREP) "^dlname=" $$f | $(SED) -e "s|^dlname='\(.*\)'|\1|"`; \
	  dirname=`dirname $(abs_builddir)/$$f `; \
	  target=$$dirname/.libs/$$soname; link=$$dirname/$$soname; \
	  shortdir=`echo $$f | $(SED) -e 's|[^/]*.la||'`; \
	  shorttarget="$${shortdir}.libs/$$soname"; \
	  echo "  LN       $$shortdir$$soname -> $$shorttarget"; \
	  rm -f  $$link; \
	  $(LN_S) $$target $$link; \
	done


clean-convenience-link:
	@for f in $^; do \
	  soname=`$(GREP) "^dlname=" $$f | $(SED) -e "s|^dlname='\(.*\)'|\1|"`; \
	  dirname=`echo $(abs_builddir)/$$f | $(SED) -e 's|/[^/]*.la||'`; \
	  target=$$dirname/.libs/$$soname; link=$$dirname/$$soname; \
	  echo "  RM       $$link"; \
	  rm -f $$link; \
	done

all-local:: convenience-link

clean-local:: clean-convenience-link
