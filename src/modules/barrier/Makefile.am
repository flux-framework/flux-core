AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

#
# Comms module
#
fluxmod_LTLIBRARIES = barrier.la

fluxmod_libadd = \
	$(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ)

fluxmod_ldflags = -avoid-version -module -shared -export-dynamic \
		  -export-symbols-regex '^mod_(main|name)$$' \
		  -L$(top_builddir)/src/lib/libcore -lflux-core

barrier_la_SOURCES = barrier.c
barrier_la_LDFLAGS = $(fluxmod_ldflags)
barrier_la_LIBADD = $(fluxmod_libadd)

#
# API for module
#
fluxcoreinclude_HEADERS = barrier.h
noinst_LTLIBRARIES = libbarrier.la
libbarrier_la_SOURCES = libbarrier.c
