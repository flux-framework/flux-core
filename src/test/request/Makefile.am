AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

#
# Comms module
#
check_LTLIBRARIES = req.la coproc.la

fluxmod_libadd = \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(JSON_LIBS) $(ZMQ_LIBS)

fluxmod_ldflags = -avoid-version -module -shared -export-dynamic \
        -export-symbols-regex '^mod_(main|name)$$'

req_la_SOURCES = req.c
# N.B. "-rpath /nowhere" forces .so build, despite noinst/check
req_la_LDFLAGS = $(fluxmod_ldflags) -rpath /nowhere
req_la_LIBADD = $(fluxmod_libadd)

coproc_la_SOURCES = coproc.c
coproc_la_LDFLAGS = $(fluxmod_ldflags) -rpath /nowhere
coproc_la_LIBADD = $(fluxmod_libadd)

#
# Test proggie
#
check_PROGRAMS = treq

fluxcmd_ldadd = \
        $(top_builddir)/src/modules/live/liblive.la \
        $(top_builddir)/src/modules/modctl/libmodctl.la \
        $(top_builddir)/src/modules/kvs/libkvs.la \
        $(top_builddir)/src/common/libflux-core.la \
        $(top_builddir)/src/common/libflux-internal.la \
        $(JSON_LIBS) $(ZMQ_LIBS) $(LIBMUNGE) $(LIBPTHREAD) $(LIBDL)

treq_SOURCES = treq.c
treq_LDADD = $(fluxcmd_ldadd)
