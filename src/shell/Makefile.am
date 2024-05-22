AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)
AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_srcdir)/src/common/libccan \
	-I$(top_builddir)/src/common/libflux \
	$(VALGRIND_CFLAGS) \
	$(LUA_INCLUDE) \
	$(HWLOC_CFLAGS) \
	$(JANSSON_CFLAGS) \
	$(LIBARCHIVE_CFLAGS)

shellrcdir = \
	$(fluxconfdir)/shell

nobase_dist_shellrc_SCRIPTS = \
	initrc.lua \
	lua.d/openmpi.lua \
	lua.d/mpi/spectrum.lua

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
	input/util.h \
	input/util.c \
	input/service.c \
	input/file.c \
	input/kvs.c \
	output.c \
	svc.c \
	svc.h \
	kill.c \
	signals.c \
	affinity.c \
	affinity.h \
	gpubind.c \
	evlog.c \
	pty.c \
	batch.c \
	tmpdir.c \
	stage-in.c \
	mpir/mpir.c \
	mpir/ptrace.c \
	mustache.h \
	mustache.c \
	doom.c \
	exception.c \
	rlimit.c \
	taskmap/cyclic.c \
	taskmap/hostfile.c \
	signal.c \
	files.c \
	oom.c \
	hwloc.c \
	rexec.c

flux_shell_LDADD = \
	$(builddir)/libshell.la \
	$(builddir)/libmpir.la \
	$(top_builddir)/src/common/libjob/libjob.la \
	$(top_builddir)/src/common/librlist/librlist.la \
	$(top_builddir)/src/bindings/lua/libfluxlua.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-taskmap.la \
	$(top_builddir)/src/common/libflux-idset.la \
	$(top_builddir)/src/common/libsubprocess/libsubprocess.la \
	$(top_builddir)/src/common/libpmi/libpmi_server.la \
	$(top_builddir)/src/common/libpmi/libpmi_common.la \
	$(top_builddir)/src/common/libczmqcontainers/libczmqcontainers.la \
	$(top_builddir)/src/common/libflux-optparse.la \
	$(top_builddir)/src/common/libterminus/libterminus.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/libfilemap/libfilemap.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(LUA_LIB) \
	$(HWLOC_LIBS) \
	$(JANSSON_LIBS) \
	$(LIBARCHIVE_LIBS)

flux_shell_LDFLAGS = \
	-export-dynamic \
	-Wl,--version-script=$(srcdir)/flux-shell.map

EXTRA_DIST = \
	flux-shell.map

TESTS = \
	test_jobspec.t \
	test_plugstack.t \
	test_mustache.t \
	mpir/test_rangelist.t \
	mpir/test_nodelist.t \
	mpir/test_proctable.t

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
