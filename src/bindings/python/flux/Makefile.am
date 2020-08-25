nobase_fluxpy_PYTHON = \
	__init__.py \
	kvs.py \
	wrapper.py \
	rpc.py \
	message.py \
	constants.py \
	job.py \
	util.py \
	future.py \
	memoized_property.py \
	debugged.py \
	core/__init__.py \
	core/watchers.py \
	core/inner.py \
	core/handle.py \
	core/trampoline.py

if HAVE_FLUX_SECURITY
nobase_fluxpy_PYTHON += security.py
endif

clean-local:
	-rm -f *.pyc */*.pyc *.pyo */*.pyo
	-rm -rf __pycache__ */__pycache__

install-data-hook:
	$(AM_V_at)echo Linking python modules in non-standard location... && \
	  $(INSTALL) -d -m 0755 "$(DESTDIR)$(fluxpylinkdir)" && \
	  target=$(fluxpydir) && \
	  f=$${target##*/} && \
	  cd "$(DESTDIR)$(fluxpylinkdir)" && \
	  rm -f $$f && \
	  $(LN_S) $$target .

uninstall-local:
	$(AM_V_at)target=$(fluxpydir) && f=$${target##*/} && \
	  echo "Removing $(fluxpylinkdir)/$$f" && \
	  rm -rf $(fluxpylinkdir)/$$f
