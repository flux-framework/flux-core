AM_CFLAGS =	$(WARNING_CFLAGS) $(CODE_COVERAGE_CFLAGS) \
		-Wno-parentheses -Wno-error=parentheses
AM_LDFLAGS =	$(CODE_COVERAGE_LIBS)
AM_CPPFLAGS =	\
	$(CODE_COVERAGE_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_srcdir)/src/common/libccan \
	-I$(top_builddir)/src/common/libflux \
	$(JANSSON_CFLAGS) \
	$(LUA_INCLUDE)

fluxluadir =     $(luadir)/flux
fluxometerdir =  $(luadir)/fluxometer
fluxluaexecdir = $(luaexecdir)/flux

nobase_dist_lua_SCRIPTS = \
	flux/timer.lua \
	flux/alt_getopt.lua \
	flux/posix.lua \
	flux/shell.lua \
	flux/Subcommander.lua

nobase_dist_fluxometer_SCRIPTS = \
	Test/Builder/NoOutput.lua \
	Test/Builder/SocketOutput.lua \
	Test/Builder/Tester.lua \
	Test/Builder/Tester/File.lua \
	Test/Builder.lua \
	Test/More.lua

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
	-Wl,--gc-sections \
	$(san_ld_zdef_flag)

luamod_libadd = \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(LUA_LIB)

flux_la_LDFLAGS = \
	$(luamod_ldflags)

flux_la_LIBADD = \
	$(luamod_libadd)

flux_la_SOURCES = \
	flux-lua.c \
	jansson-lua.c \
	jansson-lua.h \
	zmsg-lua.c \
	zmsg-lua.h \
	lutil.c \
	lutil.h

libfluxlua_la_SOURCES = \
	flux-lua.c \
	jansson-lua.c \
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
	jansson-lua.c \
	tests/json-test.c

tests_zmsgtest_la_LDFLAGS = \
	$(luamod_ldflags) -rpath /dev/null

tests_zmsgtest_la_SOURCES = \
	lutil.c \
	jansson-lua.c \
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
	Test/COPYRIGHT \
	Test/README.md \
	Test/dist.info

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh
AM_TESTS_ENVIRONMENT = \
	LUA_PATH="$(abs_srcdir)/?.lua;$LUA_PATH" \
	LUA_CPATH="$(abs_builddir)/tests/.libs/?.so;$(abs_builddir)/flux/?.so;$LUA_PATH"


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
