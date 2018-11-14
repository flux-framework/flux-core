AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux \
	-DPYTHON_LIBRARY=\"$(PYTHON_LIBRARY)\" \
	$(ZMQ_CFLAGS) \
	$(PYTHON_CPPFLAGS) \
	"-DFLUX_PYTHON_PATH=\"${pyexecdir}\""

#
# Comms module
#
fluxmod_LTLIBRARIES = pymod.la

pymod_la_SOURCES = py_mod.c
pymod_la_LDFLAGS = $(fluxmod_ldflags) -module

# allow pymod to find the configured libpython at runtime
pymod_la_LDFLAGS += -Wl,-rpath -Wl,$(PYTHON_PREFIX)/lib

pymod_la_LIBADD = $(top_builddir)/src/common/libflux-core.la \
		  $(top_builddir)/src/common/libflux-internal.la \
		  $(top_builddir)/src/common/libflux-optparse.la \
		  $(ZMQ_LIBS) \
		  $(PYTHON_LDFLAGS)

fluxpymod_PYTHON = echo.py __init__.py

