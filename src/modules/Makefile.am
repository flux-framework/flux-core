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

SUBDIRS = \
	kvs \
	content-files \
	job-ingest \
	job-manager \
	job-list \
	job-exec \
	resource \
	sdbus

if ENABLE_CONTENT_S3
SUBDIRS += content-s3
endif

fluxmod_LTLIBRARIES = \
	barrier.la \
	connector-local.la \
	content.la \
	content-files.la \
	content-sqlite.la \
	cron.la \
	heartbeat.la \
	job-archive.la \
	job-exec.la \
	job-info.la \
	job-list.la \
	job-ingest.la \
	job-manager.la \
	kvs.la \
	kvs-watch.la \
	resource.la \
	sched-simple.la \
	sdexec.la \
	sdbus.la

if ENABLE_CONTENT_S3
fluxmod_LTLIBRARIES += content-s3.la
endif

# dependencies common to kvs and content modules
content_libadd = \
	$(top_builddir)/src/common/libcontent/libcontent.la \
	$(top_builddir)/src/common/libutil/blobref.lo \
	$(top_builddir)/src/common/libutil/sha1.lo \
	$(top_builddir)/src/common/libutil/sha256.lo \
	$(top_builddir)/src/common/libutil/errprintf.lo \
	$(top_builddir)/src/common/libutil/monotime.lo \
	$(top_builddir)/src/common/libutil/tstat.lo \
	$(top_builddir)/src/common/libccan/libccan.la \
	$(top_builddir)/src/common/libczmqcontainers/libczmqcontainers.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(JANSSON_LIBS)

# dependencies common to job related modules
job_libadd = \
	$(top_builddir)/src/common/libjob/libjob.la \
	$(top_builddir)/src/common/libeventlog/libeventlog.la \
	$(top_builddir)/src/common/libutil/errprintf.lo \
	$(top_builddir)/src/common/libutil/jpath.lo \
	$(top_builddir)/src/common/libutil/fluid.lo \
	$(top_builddir)/src/common/libutil/mnemonic.lo \
	$(top_builddir)/src/common/libutil/mn_wordlist.lo \
	$(top_builddir)/src/common/libutil/basemoji.lo \
	$(top_builddir)/src/common/libutil/fsd.lo \
	$(top_builddir)/src/common/libccan/libccan.la \
	$(top_builddir)/src/common/libczmqcontainers/libczmqcontainers.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(FLUX_SECURITY_LIBS) \
	$(JANSSON_LIBS)

# non-hwloc dependent portion of librlist
rlist_libadd = \
	$(top_builddir)/src/common/librlist/rlist.lo \
	$(top_builddir)/src/common/librlist/rnode.lo \
	$(top_builddir)/src/common/librlist/match.lo

# N.B. SOURCES should be empty when module subdir is listed in
# SUBDIRS above.  This avoids distcheck problems with older automake.
# Hint: build convenience library in subdir and reference that in LIBADD.

barrier_la_SOURCES = \
	barrier/barrier.c
barrier_la_LIBADD = \
	$(top_builddir)/src/common/libczmqcontainers/libczmqcontainers.la \
	$(top_builddir)/src/common/libflux-core.la
barrier_la_LDFLAGS = $(fluxmod_ldflags) -module

connector_local_la_SOURCES = \
	connector-local/local.c
connector_local_la_LIBADD = \
	$(top_builddir)/src/common/librouter/librouter.la \
	$(top_builddir)/src/common/libflux/handle_requeue.lo \
	$(top_builddir)/src/common/libflux/msg_deque.lo \
	$(top_builddir)/src/common/libutil/log.lo \
	$(top_builddir)/src/common/libutil/aux.lo \
	$(top_builddir)/src/common/libutil/errprintf.lo \
	$(top_builddir)/src/common/libutil/fdutils.lo \
	$(top_builddir)/src/common/libutil/cleanup.lo \
	$(top_builddir)/src/common/libutil/unlink_recursive.lo \
	$(top_builddir)/src/common/libutil/dirwalk.lo \
	$(top_builddir)/src/common/libczmqcontainers/libczmqcontainers.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(LIBUUID_LIBS)
connector_local_la_LDFLAGS = $(fluxmod_ldflags) -module

content_la_SOURCES = \
	content/main.c \
	content/cache.c \
	content/cache.h \
	content/mmap.c \
	content/mmap.h \
	content/checkpoint.c \
	content/checkpoint.h
content_la_LIBADD = \
	$(top_builddir)/src/common/libutil/fileref.lo \
	$(top_builddir)/src/common/libutil/hola.lo \
	$(top_builddir)/src/common/libutil/read_all.lo \
	$(content_libadd)
content_la_LDFLAGS = $(fluxmod_ldflags) -module

content_files_la_SOURCES =
content_files_la_LIBADD = \
	$(builddir)/content-files/libcontent-files.la \
	$(top_builddir)/src/common/libutil/unlink_recursive.lo \
	$(top_builddir)/src/common/libutil/dirwalk.lo \
	$(top_builddir)/src/common/libutil/read_all.lo \
	$(content_libadd)
content_files_la_LDFLAGS = $(fluxmod_ldflags) -module

if ENABLE_CONTENT_S3
content_s3_la_SOURCES =
content_s3_la_LIBADD = \
	$(builddir)/content-s3/libcontent-s3.la \
	$(top_builddir)/src/common/libutil/tomltk.lo \
	$(top_builddir)/src/common/libutil/timestamp.lo \
	$(top_builddir)/src/common/libtomlc99/libtomlc99.la \
	$(top_builddir)/src/common/libyuarel/libyuarel.la \
	$(content_libadd) \
	$(LIBS3)
content_s3_la_LDFLAGS = $(fluxmod_ldflags) -module
endif

content_sqlite_la_SOURCES = \
        content-sqlite/content-sqlite.c
content_sqlite_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(SQLITE_CFLAGS) \
	$(LZ4_CFLAGS)
content_sqlite_la_LIBADD = \
	$(content_libadd) \
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
	$(top_builddir)/src/common/libutil/cronodate.lo \
	$(top_builddir)/src/common/libutil/fsd.lo \
	$(top_builddir)/src/common/libczmqcontainers/libczmqcontainers.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-idset.la \
	$(JANSSON_LIBS)
cron_la_LDFLAGS = $(fluxmod_ldflags) -module

heartbeat_la_SOURCES = \
	heartbeat/heartbeat.c
heartbeat_la_LIBADD = \
	$(top_builddir)/src/common/libutil/fsd.lo \
	$(top_builddir)/src/common/libflux-core.la
heartbeat_la_LDFLAGS = $(fluxmod_ldflags) -module

job_archive_la_SOURCES = \
	job-archive/job-archive.c
job_archive_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(SQLITE_CFLAGS) \
	$(FLUX_SECURITY_CFLAGS)
job_archive_la_LIBADD = \
	$(top_builddir)/src/common/libutil/fsd.lo \
	$(top_builddir)/src/common/libutil/tstat.lo \
	$(top_builddir)/src/common/libutil/monotime.lo \
	$(top_builddir)/src/common/libflux-core.la \
	$(JANSSON_LIBS) \
	$(SQLITE_LIBS)
job_archive_la_LDFLAGS = $(fluxmod_ldflags) -module

job_exec_la_SOURCES =
job_exec_la_LIBADD = \
	$(builddir)/job-exec/libjob-exec.la \
	$(builddir)/job-exec/libbulk-exec.la \
	$(top_builddir)/src/common/libutil/aux.lo \
	$(top_builddir)/src/common/libflux-idset.la \
	$(job_libadd)
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
	job-info/guest_watch.c
job_info_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(FLUX_SECURITY_CFLAGS)
job_info_la_LIBADD = \
	$(top_builddir)/src/common/libeventlog/libeventlog.la \
	$(top_builddir)/src/common/libutil/lru_cache.lo \
	$(top_builddir)/src/common/libczmqcontainers/libczmqcontainers.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(JANSSON_LIBS)
job_info_la_LDFLAGS = $(fluxmod_ldflags) -module

job_list_la_SOURCES =
job_list_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(FLUX_SECURITY_CFLAGS)
job_list_la_LIBADD = \
	$(builddir)/job-list/libjob-list.la \
	$(rlist_libadd) \
	$(top_builddir)/src/common/libutil/read_all.lo \
	$(top_builddir)/src/common/libutil/grudgeset.lo \
	$(top_builddir)/src/common/libflux-idset.la \
	$(top_builddir)/src/common/libflux-hostlist.la \
	$(job_libadd)
job_list_la_LDFLAGS = $(fluxmod_ldflags) -module

job_ingest_la_SOURCES =
job_ingest_la_CPPFLAGS = \
	$(AM_CPPFLAGS)
job_ingest_la_LIBADD = \
	$(builddir)/job-ingest/libingest.la \
	$(top_builddir)/src/common/libfluxutil/libfluxutil.la \
	$(job_libadd)
job_ingest_la_LDFLAGS = $(fluxmod_ldflags) -module

job_manager_la_SOURCES =
job_manager_la_LIBADD = \
	$(builddir)/job-manager/libjob-manager.la \
	$(top_builddir)/src/common/libfluxutil/libfluxutil.la \
	$(top_builddir)/src/common/libutil/hola.lo \
	$(top_builddir)/src/common/libutil/slice.lo \
	$(top_builddir)/src/common/libutil/aux.lo \
	$(top_builddir)/src/common/libutil/grudgeset.lo \
	$(job_libadd)
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
	$(top_builddir)/src/common/libflux/handle_requeue.lo \
	$(top_builddir)/src/common/libflux/msg_deque.lo \
	$(content_libadd) \
	$(top_builddir)/src/common/libutil/fsd.lo \
	$(top_builddir)/src/common/libutil/timestamp.lo \
	$(top_builddir)/src/common/libflux-core.la \
	$(JANSSON_LIBS)
kvs_la_LDFLAGS = $(fluxmod_ldflags) -module

kvs_watch_la_SOURCES = \
	kvs-watch/kvs-watch.c
kvs_watch_la_LIBADD = \
	$(top_builddir)/src/common/libkvs/libkvs.la \
	$(content_libadd) \
	$(top_builddir)/src/common/libflux-core.la \
	$(JANSSON_LIBS)
kvs_watch_la_LDFLAGS = $(fluxmod_ldflags) -module

resource_la_SOURCES =
resource_la_CPPFLAGS = \
	$(AM_CPPFLAGS)
resource_la_LIBADD = \
	$(builddir)/resource/libresource.la \
	$(top_builddir)/src/common/librlist/librlist.la \
	$(top_builddir)/src/common/libutil/errprintf.lo \
	$(top_builddir)/src/common/libutil/dirwalk.lo \
	$(top_builddir)/src/common/libutil/read_all.lo \
	$(top_builddir)/src/common/libeventlog/libeventlog.la \
	$(top_builddir)/src/common/libczmqcontainers/libczmqcontainers.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-idset.la \
	$(top_builddir)/src/common/libflux-hostlist.la \
	$(JANSSON_LIBS) \
	$(HWLOC_LIBS)
resource_la_LDFLAGS = $(fluxmod_ldflags) -module

sched_simple_la_SOURCES = \
	sched-simple/sched.c
sched_simple_la_CPPFLAGS = \
	$(AM_CPPFLAGS)
sched_simple_la_LIBADD = \
	$(rlist_libadd) \
	$(top_builddir)/src/common/libschedutil/libschedutil.la \
	$(top_builddir)/src/common/libutil/read_all.lo \
	$(top_builddir)/src/common/libflux-idset.la \
	$(top_builddir)/src/common/libflux-hostlist.la \
	$(job_libadd)
sched_simple_la_LDFLAGS = $(fluxmod_ldflags) -module

sdexec_la_SOURCES = \
	sdexec/sdexec.c
sdexec_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(LIBUUID_CFLAGS)
sdexec_la_LIBADD = \
	$(top_builddir)/src/common/libsdexec/libsdexec.la \
	$(top_builddir)/src/common/libutil/aux.lo \
	$(top_builddir)/src/common/libutil/errprintf.lo \
	$(top_builddir)/src/common/libutil/fdutils.lo \
	$(top_builddir)/src/common/libutil/parse_size.lo \
	$(top_builddir)/src/common/libioencode/libioencode.la \
	$(top_builddir)/src/common/libccan/libccan.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-idset.la \
	$(LIBUUID_LIBS) \
	$(JANSSON_LIBS)
sdexec_la_LDFLAGS = $(fluxmod_ldflags) -module

sdbus_la_SOURCES =
sdbus_la_LIBADD = \
	$(builddir)/sdbus/libsdbus.la \
	$(top_builddir)/src/common/libutil/errprintf.lo \
	$(top_builddir)/src/common/libflux-core.la \
	$(JANSSON_LIBS)
if HAVE_LIBSYSTEMD
sdbus_la_LIBADD += $(LIBSYSTEMD_LIBS)
endif
sdbus_la_LDFLAGS = $(fluxmod_ldflags) -module
