AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)
AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux \
	$(VALGRIND_CFLAGS) \
	$(LUA_INCLUDE) \
	$(HWLOC_CFLAGS) \
	$(JANSSON_CFLAGS)

shellrcdir = \
	$(fluxrcdir)/shell
shellluadir = \
	$(shellrcdir)/lua.d

dist_shellrc_SCRIPTS = \
	initrc.lua

dist_shelllua_SCRIPTS = \
	lua.d/00-lib.lua \
	lua.d/mvapich.lua \
	lua.d/intel_mpi.lua \
	lua.d/openmpi.lua \
	lua.d/spectrum.lua

noinst_LTLIBRARIES = \
	libshell.la \
	libmpir.la

libshell_la_SOURCES = \
	plugstack.c \
	plugstack.h \
	jobspec.c \
	jobspec.h \
	rcalc.c \
	rcalc.h

libmpir_la_SOURCES = \
	mpir/rangelist.c \
	mpir/rangelist.h \
	mpir/nodelist.c \
	mpir/nodelist.h \
	mpir/proctable.c \
	mpir/proctable.h


fluxinclude_HEADERS = \
	shell.h

fluxlibexec_PROGRAMS = flux-shell

flux_shell_SOURCES = \
	shell.c \
	shell.h \
	internal.h \
	rc.c \
	rc.h \
	builtins.c \
	builtins.h \
	info.c \
	info.h \
	task.c \
	task.h \
	log.c \
	log.h \
	events.c \
	events.h \
	pmi/pmi.c \
	pmi/pmi_exchange.c \
	pmi/pmi_exchange.h \
	input.c \
	output.c \
	svc.c \
	svc.h \
	kill.c \
	signals.c \
	affinity.c \
	gpubind.c \
	evlog.c \
	pty.c \
	batch.c \
	tmpdir.c \
	mpir/mpir.c \
	mpir/ptrace.c \
	mustache.h \
	mustache.c \
	doom.c

flux_shell_LDADD = \
	$(builddir)/libshell.la \
	$(builddir)/libmpir.la \
	$(top_builddir)/src/bindings/lua/libfluxlua.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libpmi/libpmi_server.la \
	$(top_builddir)/src/common/libczmqcontainers/libczmqcontainers.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-optparse.la \
	$(top_builddir)/src/common/libterminus/libterminus.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(LUA_LIB) \
	$(HWLOC_LIBS) \
	$(JANSSON_LIBS)

flux_shell_LDFLAGS = \
	-export-dynamic \
	-Wl,--version-script=$(srcdir)/flux-shell.map

if SHELL_PMIX
shell_plugin_LTLIBRARIES = \
	plugins/pmix/pmix.la
endif

plugins_pmix_pmix_la_SOURCES = \
	plugins/pmix/pmix.c \
	plugins/pmix/server.h \
	plugins/pmix/server.c \
	plugins/pmix/codec.h \
	plugins/pmix/codec.c \
	plugins/pmix/synchro.h \
	plugins/pmix/synchro.c \
	plugins/pmix/infovec.h \
	plugins/pmix/infovec.c \
	plugins/pmix/map.h \
	plugins/pmix/map.c \
	plugins/pmix/dmodex.h \
	plugins/pmix/dmodex.c \
	plugins/pmix/socketpair.h \
	plugins/pmix/socketpair.c \
	pmi/pmi_exchange.c \
	pmi/pmi_exchange.h
plugins_pmix_pmix_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(PMIX_CFLAGS) \
	$(ZMQ_CFLAGS)
plugins_pmix_pmix_la_LIBADD = \
	$(builddir)/libshell.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libzmqutil/libzmqutil.la \
	$(top_builddir)/src/common/librlist/librlist.la \
	$(PMIX_LIBS) \
	$(ZMQ_LIBS)
plugins_pmix_pmix_la_LDFLAGS = \
	$(AM_LDFLAGS) \
	$(fluxplugin_ldflags) \
	-module


EXTRA_DIST = \
	flux-shell.map

TESTS = \
	test_jobspec.t \
	test_plugstack.t \
	test_mustache.t \
	mpir/test_rangelist.t \
	mpir/test_nodelist.t \
	mpir/test_proctable.t

if SHELL_PMIX
TESTS += \
	plugins/pmix/test/codec.t
endif


test_ldadd = \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libpmi/libpmi_server.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libtap/libtap.la

test_ldflags = \
	-no-install

test_cppflags = \
        -I$(top_srcdir)/src/common/libtap \
        $(AM_CPPFLAGS)


check_PROGRAMS = $(TESTS)

check_LTLIBRARIES = \
	test/a/plugin.la \
	test/b/plugin.la \
	test/c/plugin.la

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
       $(top_srcdir)/config/tap-driver.sh

test_jobspec_t_SOURCES = test/jobspec.c
test_jobspec_t_CPPFLAGS = $(test_cppflags)
test_jobspec_t_LDADD = \
	$(builddir)/libshell.la \
	$(test_ldadd)
test_jobspec_t_LDFLAGS = \
	$(test_ldflags)

test_plugstack_t_SOURCES = plugstack.c test/plugstack.c
test_plugstack_t_CPPFLAGS = $(test_cppflags) -DPLUGSTACK_STANDALONE
test_plugstack_t_LDADD = \
	$(builddir)/libshell.la \
	$(test_ldadd)
test_plugstack_t_LDFLAGS = \
	$(test_ldflags)

test_a_plugin_la_SOURCES = test/plugin_test.c
test_a_plugin_la_CPPFLAGS = $(test_cppflags) -DTEST_PLUGIN_RESULT=\"A\"
test_a_plugin_la_LDFLAGS = -module -rpath /nowhere $(test_ldflags)

test_b_plugin_la_SOURCES = test/plugin_test.c
test_b_plugin_la_CPPFLAGS = $(test_cppflags) -DTEST_PLUGIN_RESULT=\"B\"
test_b_plugin_la_LDFLAGS = -module -rpath /nowhere $(test_ldflags)

test_c_plugin_la_SOURCES = test/plugin_test.c
test_c_plugin_la_CPPFLAGS = $(test_cppflags) -DTEST_PLUGIN_RESULT=\"C\"
test_c_plugin_la_LDFLAGS = -module -rpath /nowhere $(test_ldflags)

mpir_test_rangelist_t_SOURCES = mpir/test/rangelist.c
mpir_test_rangelist_t_CPPFLAGS = $(test_cppflags)
mpir_test_rangelist_t_LDADD = \
	$(builddir)/libmpir.la \
	$(test_ldadd)
mpir_test_rangelist_t_LDFLAGS = \
	$(test_ldflags)

mpir_test_nodelist_t_SOURCES = mpir/test/nodelist.c
mpir_test_nodelist_t_CPPFLAGS = $(test_cppflags)
mpir_test_nodelist_t_LDADD = \
	$(builddir)/libmpir.la \
	$(test_ldadd)
mpir_test_nodelist_t_LDFLAGS = \
	$(test_ldflags)

mpir_test_proctable_t_SOURCES = mpir/test/proctable.c
mpir_test_proctable_t_CPPFLAGS = $(test_cppflags)
mpir_test_proctable_t_LDADD = \
	$(builddir)/libmpir.la \
	$(test_ldadd)
mpir_test_proctable_t_LDFLAGS = \
	$(test_ldflags)

test_mustache_t_SOURCES = \
	mustache.c \
	mustache.h \
	test/mustache.c
test_mustache_t_CPPFLAGS = \
	$(test_cppflags)
test_mustache_t_LDADD = \
	$(builddir)/libshell.la \
	$(test_ldadd)
test_mustache_t_LDFLAGS = \
	$(test_ldflags)

plugins_pmix_test_codec_t_SOURCES = \
	plugins/pmix/codec.c \
	plugins/pmix/test/codec.c
plugins_pmix_test_codec_t_CPPFLAGS = \
	$(PMIX_CFLAGS) \
	$(test_cppflags)
plugins_pmix_test_codec_t_LDADD = \
	$(JANSSON_LIBS) \
	$(test_ldadd)
plugins_pmix_test_codec_t_LDFLAGS = \
	$(test_ldflags)

.PHONY: link-shell-plugins clean-shell-plugins

link-shell-plugins: $(shell_plugin_LTLIBRARIES)
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

clean-shell-plugin-links:
	@for f in $(shell_plugin_LTLIBRARIES); do \
	 soname=`$(GREP) "^dlname=" $$f | $(SED) -e "s|^dlname='\(.*\)'|\1|"`; \
	 dirname=`echo $(abs_builddir)/$$f | $(SED) -e 's|/[^/]*.la||'`; \
	 target=$$dirname/.libs/$$soname; link=$$dirname/$$soname; \
	 echo "  RM       $$link"; \
	 rm -f $$link; \
	done

all-local:: link-shell-plugins

clean-local:: clean-shell-plugin-links
