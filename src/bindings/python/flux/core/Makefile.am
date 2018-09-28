fluxpycoredir = $(fluxpydir)/core
fluxpycore_PYTHON=\
	      __init__.py\
	      watchers.py\
	      inner.py\
	      handle.py\
	      trampoline.py

# Support VPATH builds
lib-copy-vpath: ${fluxpycore_PYTHON}
	[ "$(top_srcdir)" != "$(top_builddir)" ] && cp $(top_srcdir)/src/bindings/python/flux/core/*.py ./ || true

all-local: lib-copy-vpath

# Support VPATH builds
clean-local-vpath:
	[ "$(top_srcdir)" != "$(top_builddir)" ] && rm -f $(top_builddir)/src/bindings/python/flux/core/*.py || true

clean-local: clean-local-vpath
	-rm -f *.c *_build.py *.so *.pyc *.pyo
	-rm -rf __pycache__
