AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	$(CODE_COVERAGE_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux \
	-I$(top_srcdir)/src/common/libccan \
	$(JANSSON_CFLAGS)

SUBDIRS = \
	connector-local \
	kvs \
	content-files \
	job-ingest \
	job-manager \
	job-list \
	job-exec \
	resource \
	sdbus

fluxmod_LTLIBRARIES = \
	barrier.la \
	content.la \
	content-files.la \
	content-sqlite.la \
	cron.la \
	heartbeat.la \
	job-exec.la \
	job-info.la \
	job-list.la \
	job-ingest.la \
	job-manager.la \
	kvs.la \
	kvs-watch.la \
	resource.la \
	sched-simple.la \
	sdmon.la \
	sdexec.la \
	sdbus.la

# N.B. SOURCES should be empty when module subdir is listed in
# SUBDIRS above.  This avoids distcheck problems with older automake.
# Hint: build convenience library in subdir and reference that in LIBADD.

barrier_la_SOURCES = \
	barrier/barrier.c
barrier_la_LIBADD = \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la
barrier_la_LDFLAGS = $(fluxmod_ldflags) -module

content_la_SOURCES = \
	content/main.c \
	content/cache.c \
	content/cache.h \
	content/mmap.c \
	content/mmap.h \
	content/checkpoint.c \
	content/checkpoint.h
content_la_LIBADD = \
	$(top_builddir)/src/common/libfilemap/libfilemap.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(LIBARCHIVE_LIBS)
content_la_LDFLAGS = $(fluxmod_ldflags) -module

content_files_la_SOURCES =
content_files_la_LIBADD = \
	$(builddir)/content-files/libcontent-files.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la
content_files_la_LDFLAGS = $(fluxmod_ldflags) -module

content_sqlite_la_SOURCES = \
        content-sqlite/content-sqlite.c
content_sqlite_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(SQLITE_CFLAGS) \
	$(LZ4_CFLAGS)
content_sqlite_la_LIBADD = \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(SQLITE_LIBS) \
	$(LZ4_LIBS)
content_sqlite_la_LDFLAGS = $(fluxmod_ldflags) -module

cron_la_SOURCES = \
        cron/cron.c \
	cron/task.h \
	cron/task.c \
	cron/entry.h \
	cron/types.h \
	cron/types.c \
	cron/interval.c \
	cron/event.c \
	cron/datetime.c
cron_la_LIBADD = \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(JANSSON_LIBS)
cron_la_LDFLAGS = $(fluxmod_ldflags) -module

heartbeat_la_SOURCES = \
	heartbeat/heartbeat.c
heartbeat_la_LIBADD = \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la
heartbeat_la_LDFLAGS = $(fluxmod_ldflags) -module

job_exec_la_SOURCES =
job_exec_la_LIBADD = \
	$(builddir)/job-exec/libjob-exec.la \
	$(top_builddir)/src/common/libjob/libjob.la \
	$(top_builddir)/src/common/libsubprocess/libsubprocess.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(JANSSON_LIBS)
job_exec_la_LDFLAGS = $(fluxmod_ldflags) -module

job_info_la_SOURCES = \
	job-info/job-info.h \
	job-info/job-info.c \
	job-info/allow.h \
	job-info/allow.c \
	job-info/lookup.h \
	job-info/lookup.c \
	job-info/watch.h \
	job-info/watch.c \
	job-info/guest_watch.h \
	job-info/guest_watch.c \
	job-info/update.h \
	job-info/update.c \
	job-info/util.h \
	job-info/util.c
job_info_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(FLUX_SECURITY_CFLAGS)
job_info_la_LIBADD = \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libjob/libjob.la \
	$(JANSSON_LIBS)
job_info_la_LDFLAGS = $(fluxmod_ldflags) -module

job_list_la_SOURCES =
job_list_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(FLUX_SECURITY_CFLAGS) \
	$(HWLOC_CFLAGS)
job_list_la_LIBADD = \
	$(builddir)/job-list/libjob-list.la \
	$(top_builddir)/src/common/libjob/libjob.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-optparse.la \
	$(top_builddir)/src/common/librlist/librlist.la \
	$(JANSSON_LIBS)
job_list_la_LDFLAGS = $(fluxmod_ldflags) -module

job_ingest_la_SOURCES =
job_ingest_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(HWLOC_CFLAGS)
job_ingest_la_LIBADD = \
	$(builddir)/job-ingest/libingest.la \
	$(top_builddir)/src/common/libjob/libjob.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-optparse.la \
	$(JANSSON_LIBS) \
	$(FLUX_SECURITY_LIBS)
job_ingest_la_LDFLAGS = $(fluxmod_ldflags) -module

job_manager_la_SOURCES =
job_manager_la_LIBADD = \
	$(builddir)/job-manager/libjob-manager.la \
	$(top_builddir)/src/common/libjob/libjob.la \
	$(top_builddir)/src/common/libsubprocess/libsubprocess.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libflux-optparse.la \
	$(top_builddir)/src/common/librlist/librlist.la \
	$(JANSSON_LIBS)
job_manager_la_LDFLAGS = \
	$(fluxlib_ldflags) \
	-avoid-version \
	-export-symbols-regex \
	'^(flux_jobtap.*|mod_main)$$' \
	-module

kvs_la_SOURCES =
kvs_la_LIBADD = \
	$(builddir)/kvs/libkvs.la \
	$(top_builddir)/src/common/libkvs/libkvs.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(JANSSON_LIBS)
kvs_la_LDFLAGS = $(fluxmod_ldflags) -module

kvs_watch_la_SOURCES = \
	kvs-watch/kvs-watch.c
kvs_watch_la_LIBADD = \
	$(top_builddir)/src/common/libkvs/libkvs.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-optparse.la \
	$(JANSSON_LIBS)
kvs_watch_la_LDFLAGS = $(fluxmod_ldflags) -module

resource_la_SOURCES =
resource_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(HWLOC_CFLAGS)
resource_la_LIBADD = \
	$(builddir)/resource/libresource.la \
	$(top_builddir)/src/common/librlist/librlist-hwloc.la \
	$(top_builddir)/src/common/librlist/librlist.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(JANSSON_LIBS) \
	$(HWLOC_LIBS)
resource_la_LDFLAGS = $(fluxmod_ldflags) -module

sched_simple_la_SOURCES = \
	sched-simple/sched.c
sched_simple_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(HWLOC_CFLAGS)
sched_simple_la_LIBADD = \
	$(top_builddir)/src/common/librlist/librlist.la \
	$(top_builddir)/src/common/libschedutil/libschedutil.la \
	$(top_builddir)/src/common/libjob/libjob.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-optparse.la \
	$(JANSSON_LIBS)
sched_simple_la_LDFLAGS = $(fluxmod_ldflags) -module

sdmon_la_SOURCES = \
	sdmon/sdmon.c
sdmon_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(LIBUUID_CFLAGS)
sdmon_la_LIBADD = \
	$(top_builddir)/src/common/libsdexec/libsdexec.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(LIBUUID_LIBS) \
	$(JANSSON_LIBS)
sdmon_la_LDFLAGS = $(fluxmod_ldflags) -module

sdexec_la_SOURCES = \
	sdexec/sdexec.c
sdexec_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(LIBUUID_CFLAGS)
sdexec_la_LIBADD = \
	$(top_builddir)/src/common/libsdexec/libsdexec.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(LIBUUID_LIBS) \
	$(JANSSON_LIBS)
sdexec_la_LDFLAGS = $(fluxmod_ldflags) -module

sdbus_la_SOURCES =
sdbus_la_LIBADD = \
	$(builddir)/sdbus/libsdbus.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(JANSSON_LIBS)
if HAVE_LIBSYSTEMD
sdbus_la_LIBADD += $(LIBSYSTEMD_LIBS)
endif
sdbus_la_LDFLAGS = $(fluxmod_ldflags) -module
