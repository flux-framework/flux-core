AM_CPPFLAGS = \
	$(WARNING_CFLAGS) -Wno-error=missing-field-initializers \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	-I$(top_srcdir)/src/common/libflux \
	-I$(top_builddir)/src/common/libflux \
	$(ZMQ_CFLAGS) $(PYTHON_CPPFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	-avoid-version -module $(san_ld_zdef_flag) \
	-Wl,-rpath,$(PYTHON_PREFIX)/lib \
	$(CODE_COVERAGE_LIBS)

MAKE_BINDING=$(top_srcdir)/src/bindings/python/make_binding.py
SUFFIXES = _build.py

common_libs = $(top_builddir)/src/common/libflux-core.la \
	      $(top_builddir)/src/common/libflux-internal.la \
	      $(ZMQ_LIBS) \
	      $(PYTHON_LDFLAGS)

_build.py.c:
	$(PYTHON) $*_build.py


_core_build.py: $(MAKE_BINDING)
	$(PYTHON) $(MAKE_BINDING) --path $(top_srcdir) \
		--search $(top_builddir)/src/common/libflux \
		--package _flux \
		--modname _core \
		--add_sub '.*va_list.*|||' \
		--additional_headers src/bindings/python/_flux/callbacks.h \
		--ignore_header 'macros' \
		src/include/flux/core.h

BUILT_SOURCES= _core.c
fluxpyso_LTLIBRARIES = _core.la
fluxpyso_PYTHON = __init__.py

dist__core_la_SOURCES = callbacks.h
nodist__core_la_SOURCES = _core.c
_core_la_LIBADD = $(common_libs)
_core_la_DEPENDENCIES = _core_build.py

if HAVE_FLUX_SECURITY
BUILT_SOURCES += _security.c
fluxpyso_LTLIBRARIES += _security.la
_security_build.py: $(MAKE_BINDING)
	$(PYTHON) $(MAKE_BINDING) --path $(FLUX_SECURITY_INCDIR)/flux/security \
		--package _flux \
		--modname _security \
		--include_header flux/security/sign.h \
		--add_sub '.*va_list.*|||' \
		sign.h

nodist__security_la_SOURCES = _security.c
_security_la_CPPFLAGS = $(AM_CPPFLAGS) $(FLUX_SECURITY_CFLAGS)
_security_la_LIBADD = $(common_libs) $(FLUX_SECURITY_LIBS)
_security_la_DEPENDENCIES = _security_build.py
endif

.PHONY: lib-copy

lib-copy-vpath: ${fluxpyso_PYTHON} ${fluxpyso_LTLIBRARIES}
	-echo Copying libraries to where they can be used by python in-tree
	[ "$(top_srcdir)" != "$(top_builddir)" ] && cp $(top_srcdir)/src/bindings/python/_flux/__init__.py ./; \
	for LIB in ${fluxpyso_LTLIBRARIES:la=so} ; do \
		test -e .libs/$$LIB && \
		$(LN_S) .libs/$$LIB ./ || true; \
	done

all-local: lib-copy-vpath

# Support VPATH builds
clean-local-vpath:
	[ "$(top_srcdir)" != "$(top_builddir)" ] && rm -f $(top_builddir)/src/bindings/python/_flux/*.py || true

clean-local: clean-local-vpath
	-rm -f *.c *_build.py *.so *.pyc *.pyo
	-rm -rf __pycache__
