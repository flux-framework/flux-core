fluxpycoredir = $(fluxpydir)/core
fluxpycore_PYTHON=\
	      __init__.py\
	      watchers.py\
	      inner.py\
	      handle.py\
	      trampoline.py

#TODO: there must be a better way to do this
check-local:
	-echo Copying libraries to where they can be used by python for check
	[ ! -e ./__init__.py ] && cp -s $(top_srcdir)/src/bindings/python/flux/core/*.py ./ || true

clean-local: clean-local-check
	-rm -f *.c *_build.py *.so *.pyc *.pyo

.PHONY: clean-local-check
clean-local-check:
	find -name '*.py' -and -type s -print0 | xargs -0 rm -f
# NOTE: Removes symlinks to python files used by check

