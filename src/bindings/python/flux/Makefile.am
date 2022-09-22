nobase_fluxpy_PYTHON = \
	__init__.py \
	kvs.py \
	wrapper.py \
	rpc.py \
	message.py \
	constants.py \
	util.py \
	future.py \
	memoized_property.py \
	debugged.py \
	importer.py \
	core/__init__.py \
	core/watchers.py \
	core/inner.py \
	core/handle.py \
	core/trampoline.py \
	job/__init__.py \
	job/JobID.py \
	job/Jobspec.py \
	job/event.py \
	job/kill.py \
	job/kvs.py \
	job/list.py \
	job/info.py \
	job/submit.py \
	job/wait.py \
	job/stats.py \
	job/_wrapper.py \
	job/executor.py \
	job/validator/__init__.py \
	job/validator/validator.py \
	job/validator/plugins/jobspec.py \
	job/validator/plugins/schema.py \
	job/validator/plugins/feasibility.py \
	job/validator/plugins/require-instance.py \
	job/frobnicator/__init__.py \
	job/frobnicator/frobnicator.py \
	job/frobnicator/plugins/defaults.py \
	job/frobnicator/plugins/constraints.py \
	resource/Rlist.py \
	resource/__init__.py \
	resource/ResourceSetImplementation.py \
	resource/ResourceSet.py \
	resource/list.py \
	hostlist.py \
	idset.py \
	progress.py \
	uri/uri.py \
	uri/__init__.py \
	uri/resolvers/jobid.py \
	uri/resolvers/pid.py \
	uri/resolvers/slurm.py \
	uri/resolvers/lsf.py \
	utils/parsedatetime/__init__.py \
	utils/parsedatetime/parsedatetime.py \
	utils/parsedatetime/warns.py \
	utils/parsedatetime/context.py \
	utils/parsedatetime/pdt_locales/__init__.py \
	utils/parsedatetime/pdt_locales/base.py \
	utils/parsedatetime/pdt_locales/icu.py \
	utils/parsedatetime/pdt_locales/de_DE.py \
	utils/parsedatetime/pdt_locales/en_US.py \
	utils/parsedatetime/pdt_locales/en_AU.py \
	utils/parsedatetime/pdt_locales/es.py \
	utils/parsedatetime/pdt_locales/fr_FR.py \
	utils/parsedatetime/pdt_locales/nl_NL.py \
	utils/parsedatetime/pdt_locales/pt_BR.py \
	utils/parsedatetime/pdt_locales/ru_RU.py \
	utils/tomli/_re.py \
	utils/tomli/_parser.py \
	utils/tomli/__init__.py \
	utils/tomli/py.typed \
	utils/tomli/LICENSE \
	utils/tomli/_types.py

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
