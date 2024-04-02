AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux \
	-I$(top_srcdir)/src/common/libccan \
	$(JANSSON_CFLAGS)

noinst_LTLIBRARIES = \
	libjob-manager.la

jobtap_plugin_LTLIBRARIES = \
	plugins/submit-hold.la \
	plugins/alloc-bypass.la \
	plugins/alloc-check.la \
	plugins/perilog.la

libjob_manager_la_SOURCES = \
	job-manager.c \
	job-manager.h \
	job.c \
	job.h \
	conf.c \
	conf.h \
	submit.c \
	submit.h \
	drain.c \
	drain.h \
	wait.c \
	wait.h \
	event.h \
	event.c \
	restart.h \
	restart.c \
	raise.h \
	raise.c \
	kill.h \
	kill.c \
	alloc.h \
	alloc.c \
	start.h \
	start.c \
	list.h \
	list.c \
	purge.h \
	purge.c \
	urgency.h \
	urgency.c \
	annotate.h \
	annotate.c \
	journal.h \
	journal.c \
	getattr.h \
	getattr.c \
	prioritize.h \
	prioritize.c \
	queue.h \
	queue.c \
	jobtap-internal.h \
	jobtap.h \
	jobtap.c \
	update.h \
	update.c \
	plugins/priority-default.c \
	plugins/limit-job-size.c \
	plugins/limit-duration.c \
	plugins/dependency-after.c \
	plugins/begin-time.c \
	plugins/update-duration.c \
	plugins/validate-duration.c \
	plugins/history.c \
	plugins/post-event.c

fluxinclude_HEADERS = \
	jobtap.h

plugins_submit_hold_la_SOURCES = \
	plugins/submit-hold.c
plugins_submit_hold_la_LDFLAGS = \
	$(fluxplugin_ldflags) \
	-module

plugins_alloc_bypass_la_SOURCES = \
	plugins/alloc-bypass.c
plugins_alloc_bypass_la_LIBADD = \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/librlist/librlist.la
plugins_alloc_bypass_la_LDFLAGS = \
	$(fluxplugin_ldflags) \
	-module

plugins_alloc_check_la_SOURCES = \
	plugins/alloc-check.c
plugins_alloc_check_la_LIBADD = \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/librlist/librlist.la
plugins_alloc_check_la_LDFLAGS = \
	$(fluxplugin_ldflags) \
	-module

plugins_perilog_la_SOURCES = \
	plugins/perilog.c
plugins_perilog_la_LIBADD = \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/librlist/librlist.la \
	$(top_builddir)/src/common/libjob/libjob.la
plugins_perilog_la_LDFLAGS = \
	$(fluxplugin_ldflags) \
	-module


TESTS = \
	test_job.t \
	test_list.t \
	test_raise.t \
	test_kill.t \
	test_restart.t \
	test_annotate.t

test_ldadd = \
	libjob-manager.la \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/common/librlist/librlist.la \
	$(top_builddir)/src/common/libjob/libjob.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(LIBPTHREAD) \
	$(JANSSON_LIBS) \
	$(HWLOC_LIBS)

test_cppflags = \
	$(AM_CPPFLAGS)

test_ldflags = \
	-no-install

check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
       $(top_srcdir)/config/tap-driver.sh

test_job_t_SOURCES = test/job.c
test_job_t_CPPFLAGS = $(test_cppflags)
test_job_t_LDADD = \
        $(test_ldadd)
test_job_t_LDFLAGS = \
        $(test_ldflags)

test_list_t_SOURCES = test/list.c
test_list_t_CPPFLAGS = $(test_cppflags)
test_list_t_LDADD = \
        $(top_builddir)/src/modules/job-manager/list.o \
	$(test_ldadd)
test_list_t_LDFLAGS = \
        $(test_ldflags)

test_raise_t_SOURCES = test/raise.c
test_raise_t_CPPFLAGS = $(test_cppflags)
test_raise_t_LDADD = \
        $(top_builddir)/src/modules/job-manager/raise.o \
        $(test_ldadd)
test_raise_t_LDFLAGS = \
        $(test_ldflags)

test_kill_t_SOURCES = test/kill.c
test_kill_t_CPPFLAGS = $(test_cppflags)
test_kill_t_LDADD = \
        $(top_builddir)/src/modules/job-manager/kill.o \
        $(test_ldadd)
test_kill_t_LDFLAGS = \
        $(test_ldflags)

test_restart_t_SOURCES = test/restart.c
test_restart_t_CPPFLAGS = $(test_cppflags)
test_restart_t_LDADD = \
        $(top_builddir)/src/modules/job-manager/restart.o \
        $(test_ldadd)
test_restart_t_LDFLAGS = \
        $(test_ldflags)

test_annotate_t_SOURCES = test/annotate.c
test_annotate_t_CPPFLAGS = $(test_cppflags)
test_annotate_t_LDADD = \
        $(test_ldadd)
test_annotate_t_LDFLAGS = \
        $(test_ldflags)
