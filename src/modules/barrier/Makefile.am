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

general_ldflags = --disable-static -avoid-version -shared -export-dynamic \
		  $(top_builddir)/src/common/libflux-internal.la \
		  -L$(top_builddir)/src/common -lflux-core
fluxmod_ldflags = -module \
		  -export-symbols-regex '^mod_(main|name)$$' \
		  $(general_ldflags)

barrier_la_SOURCES = barrier.c
barrier_la_LDFLAGS = $(fluxmod_ldflags)
barrier_la_LIBADD = $(fluxmod_libadd)

#
# API for module
#
fluxcoreinclude_HEADERS = barrier.h
fluxlib_LTLIBRARIES = libbarrier.la
libbarrier_la_SOURCES = libbarrier.c
libbarrier_la_LDFLAGS = $(general_ldflags) 
