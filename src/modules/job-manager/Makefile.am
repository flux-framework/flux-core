AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	$(ZMQ_CFLAGS)

fluxmod_LTLIBRARIES = job-manager.la

job_manager_la_SOURCES = job-manager.c \
			 job.c \
			 job.h \
			 queue.c \
			 queue.h \
			 active.h \
			 active.c \
			 purge.h \
			 purge.c \
			 list.h \
			 list.c \
			 priority.h \
			 priority.c

job_manager_la_LDFLAGS = $(fluxmod_ldflags) -module
job_manager_la_LIBADD = $(fluxmod_libadd) \
		    $(top_builddir)/src/common/libflux-internal.la \
		    $(top_builddir)/src/common/libflux-core.la \
		    $(top_builddir)/src/common/libflux-optparse.la \
		    $(ZMQ_LIBS)
