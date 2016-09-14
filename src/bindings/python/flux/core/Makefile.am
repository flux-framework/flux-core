fluxpycoredir = $(fluxpydir)/core
fluxpycore_PYTHON=\
	      __init__.py\
	      watchers.py\
	      inner.py\
	      handle.py\
	      trampoline.py

lib-copy:
	-echo Copying core libraries to where they can be used by python out of tree
	[ "$(top_srcdir)" != "$(top_builddir)" ] && cp  $(top_srcdir)/src/bindings/python/flux/core/*.py ./ || true

#TODO: there must be a better way to do this

check-local: lib-copy
all-local: lib-copy

clean-local: clean-local-check
	-rm -f *.c *_build.py *.so *.pyc *.pyo

.PHONY: clean-local-check lib-copy
clean-local-check:
	[ "$(top_srcdir)" != "$(top_builddir)" ] && rm -f $(top_builddir)/src/bindings/python/flux/core/*.py || true
# NOTE: Removes symlinks to python files used by check

