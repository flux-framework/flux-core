AM_CPPFLAGS = \
	$(WARNING_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	-I$(top_srcdir)/src/common/libflux \
	-I$(top_builddir)/src/common/libflux \
	$(ZMQ_CFLAGS) $(PYTHON_CPPFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LIBADD = \
	$(PYTHON_LDFLAGS) 
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
		--ignore_header 'macros' \
		src/include/flux/core.h

BUILT_SOURCES= _core.c
fluxso_LTLIBRARIES = _core.la
fluxso_PYTHON = __init__.py

nodist__core_la_SOURCES = _core.c
_core_la_LIBADD = $(common_libs)
_core_la_DEPENDENCIES = _core_build.py


_job_build.py: $(top_srcdir)/src/common/libjob/job.h $(MAKE_BINDING) _core_build.py
	$(PYTHON) $(MAKE_BINDING) --path $(top_srcdir)/src/common/libjob \
		--package _flux \
		--modname _job \
		--add_sub '.*va_list.*|||' \
		--include_ffi _core_build \
		job.h

nodist__job_la_SOURCES = _job.c $(top_srcdir)/src/common/libjob/job.h
_job_la_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/src/common/libjob
_job_la_LIBADD = $(common_libs)
_job_la_DEPENDENCIES = _job_build.py

if HAVE_FLUX_SECURITY
BUILT_SOURCES += _security.c
fluxso_LTLIBRARIES += _security.la
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

lib-copy-vpath: ${fluxso_PYTHON} ${fluxso_LTLIBRARIES}
	-echo Copying libraries to where they can be used by python in-tree
	[ "$(top_srcdir)" != "$(top_builddir)" ] && cp $(top_srcdir)/src/bindings/python/_flux/__init__.py ./; \
	for LIB in ${fluxso_LTLIBRARIES:la=so} ; do \
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
