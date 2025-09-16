AM_CPPFLAGS = \
	$(WARNING_CFLAGS) \
	-Wno-missing-field-initializers \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	-I$(top_srcdir)/src/common/libflux \
	-I$(top_builddir)/src/common/libflux \
	$(PYTHON_CPPFLAGS) \
	$(JANSSON_CFLAGS) \
	$(CODE_COVERAGE_CPPFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	-avoid-version \
	-module \
	$(san_ld_zdef_flag) \
	-Wl,-rpath,$(PYTHON_PREFIX)/lib \
	$(ld_gc_sections) \
	$(CODE_COVERAGE_LIBS)

common_libs = \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libdebugged/libdebugged.la \
	$(PYTHON_LIBS)

PREPROC_FLAGS = \
	-E '-D__attribute__(...)=' \
	-D "FLUX_DEPRECATED(...)="

fluxpyso_LTLIBRARIES = \
	_core.la \
	_hostlist.la \
	_idset.la \
	_count.la \
	_rlist.la

fluxpyso_PYTHON = \
	__init__.py

nodist_fluxbindinginclude_HEADERS = \
	_core_preproc.h \
	_hostlist_preproc.h \
	_idset_preproc.h \
	_count_preproc.h \
	_rlist_preproc.h

EXTRA_DIST = \
	_core_build.py \
	_security_build.py \
	_hostlist_build.py \
	_idset_build.py \
	_count_build.py \
	_rlist_build.py \
	make_clean_header.py

STDERR_DEVNULL = $(stderr_devnull_$(V))
stderr_devnull_ =  $(stderr_devnull_$(AM_DEFAULT_VERBOSITY))
stderr_devnull_0 = >/dev/null 2>&1

_core.c: $(srcdir)/_core_build.py _core_preproc.h
	$(AM_V_GEN)$(PYTHON) $< $(STDERR_DEVNULL)

_core_clean.h: Makefile
	$(AM_V_GEN)$(PYTHON) $(srcdir)/make_clean_header.py \
	  --path $(top_srcdir) \
	  --search $(top_builddir)/src/common/libflux \
	  --additional_headers \
	      src/bindings/python/_flux/callbacks.h \
	      src/common/libdebugged/debugged.h \
	  --output _core_clean.h \
	  src/include/flux/core.h

_core_preproc.h: _core_clean.h
	$(AM_V_GEN)$(CC) $(PREPROC_FLAGS) _core_clean.h \
	  | sed -e '/^# [0-9]*.*/d' > $@

_hostlist.c: $(srcdir)/_hostlist_build.py _hostlist_preproc.h
	$(AM_V_GEN)$(PYTHON) $^ $(STDERR_DEVNULL)

_hostlist_clean.h: Makefile
	$(AM_V_GEN)$(PYTHON) $(srcdir)/make_clean_header.py \
	  --path $(top_srcdir)/src/common/libhostlist \
	  --output $@ \
	  hostlist.h

_hostlist_preproc.h: _hostlist_clean.h
	$(AM_V_GEN)$(CC) $(PREPROC_FLAGS) $< \
	  | sed -e '/^# [0-9]*.*/d' > $@

_idset.c: $(srcdir)/_idset_build.py _idset_preproc.h
	$(AM_V_GEN)$(PYTHON) $^ $(STDERR_DEVNULL)

_idset_clean.h: Makefile
	$(AM_V_GEN)$(PYTHON) $(srcdir)/make_clean_header.py \
	  --path $(top_srcdir)/src/common/libidset \
	  --output $@ \
	  idset.h

_idset_preproc.h: _idset_clean.h
	$(AM_V_GEN)$(CC) $(PREPROC_FLAGS) $< \
	  | sed -e '/^# [0-9]*.*/d' > $@

_count.c: $(srcdir)/_count_build.py _count_preproc.h
	$(AM_V_GEN)$(PYTHON) $^ $(STDERR_DEVNULL)

_count_clean.h: Makefile
	$(AM_V_GEN)$(PYTHON) $(srcdir)/make_clean_header.py \
	  --path $(top_srcdir)/src/common/libjob \
	  --output $@ \
	  count.h

_count_preproc.h: _count_clean.h
	$(AM_V_GEN)$(CC) $(PREPROC_FLAGS) $< \
	  | sed -e '/^# [0-9]*.*/d' > $@


_rlist.c: $(srcdir)/_rlist_build.py _rlist_preproc.h
	$(AM_V_GEN)$(PYTHON) $^ $(STDERR_DEVNULL)

_rlist_clean.h: Makefile
	$(AM_V_GEN)$(PYTHON) $(srcdir)/make_clean_header.py \
	  --path $(top_srcdir)/src/common/librlist \
	  --search $(top_builddir)/config \
	  --search /usr/include \
	  --search $(top_srcdir) \
	  --ignore_header czmq_containers \
	  --output $@ \
	  rlist.h

_rlist_preproc.h: _rlist_clean.h
	$(AM_V_GEN)$(CC) $(PREPROC_FLAGS) $< \
	  | sed -e '/^# [0-9]*.*/d' > $@


dist__core_la_SOURCES = callbacks.h
nodist__core_la_SOURCES = _core.c
_core_la_LIBADD = $(common_libs)

nodist__hostlist_la_SOURCES = _hostlist.c
_hostlist_la_LIBADD = $(common_libs)

nodist__idset_la_SOURCES = _idset.c
_idset_la_LIBADD = $(common_libs)

nodist__count_la_SOURCES = _count.c
_count_la_LIBADD = \
	$(top_builddir)/src/common/libjob/libjob.la \
	$(common_libs)

nodist__rlist_la_SOURCES = _rlist.c
_rlist_la_LIBADD = \
	$(top_builddir)/src/common/librlist/librlist.la \
	$(common_libs)

if HAVE_FLUX_SECURITY

fluxpyso_LTLIBRARIES += \
	_security.la

nodist_fluxbindinginclude_HEADERS +=\
	_security_preproc.h

nodist__security_la_SOURCES = \
	_security.c

_security.c: $(srcdir)/_security_build.py _security_preproc.h
	$(AM_V_GEN)$(PYTHON) $^ $(STDERR_DEVNULL)
_security_clean.h: Makefile
	$(AM_V_GEN)$(PYTHON) $(srcdir)/make_clean_header.py \
	  --path $(FLUX_SECURITY_INCDIR)/flux/security \
	  --output _security_clean.h \
	  sign.h $(STDERR_DEVNULL)
_security_preproc.h: _security_clean.h
	$(AM_V_GEN)$(CC) $(PREPROC_FLAGS) _security_clean.h \
	  | sed -e '/^# [0-9]*.*/d' > $@

_security_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(FLUX_SECURITY_CFLAGS)
_security_la_LIBADD = \
	$(common_libs) \
	$(FLUX_SECURITY_LIBS)
endif

.PHONY: lib-copy

cp_verbose = $(cp_verbose_$(V))
cp_verbose_ = $(cp_verbose_$(AM_DEFAULT_VERBOSITY))
cp_verbose_0 = @echo "  COPY     python DSOs" ;

# Copy libraries to where they can be used by python in-tree
lib-copy-vpath: ${fluxpyso_PYTHON} ${fluxpyso_LTLIBRARIES}
	$(cp_verbose)[ "$(top_srcdir)" != "$(top_builddir)" ] && cp $(top_srcdir)/src/bindings/python/_flux/__init__.py ./; \
	for LIB in ${fluxpyso_LTLIBRARIES:la=so} ; do \
		test -e .libs/$$LIB && \
		$(LN_S) .libs/$$LIB ./ $(STDERR_DEVNULL) || true; \
	done

all-local: lib-copy-vpath

# Support VPATH builds
clean-local-vpath:
	[ "$(top_srcdir)" != "$(top_builddir)" ] && rm -f $(top_builddir)/src/bindings/python/_flux/*.py || true

clean-local: clean-local-vpath
	-rm -f *.c *.so *.pyc *.pyo *_clean.h *_preproc.h
	-rm -rf __pycache__

install-data-hook:
	$(AM_V_at)echo Linking python modules in non-standard location... && \
	  $(MKDIR_P) "$(DESTDIR)$(fluxpylinkdir)" && \
	  target=$(fluxpysodir) && \
	  f=$${target##*/} && \
	  cd "$(DESTDIR)$(fluxpylinkdir)" && \
	  rm -f $$f && \
	  $(LN_S) $$target .

uninstall-local:
	$(AM_V_at)target=$(fluxpysodir) && f=$${target##*/} && \
	  echo "Removing $(fluxpylinkdir)/$$f" && \
	  rm -rf $(fluxpylinkdir)/$$f
