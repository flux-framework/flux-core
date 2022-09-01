if HAVE_FLUX_SECURITY
all: core hostlist rlist security idset
else
all: core hostlist rlist idset
endif

core:
	$(AM_V_GEN)$(PYTHON) $(srcdir)/setup.py install --root=$(top_srcdir)

hostlist:
	$(AM_V_GEN)$(PYTHON) $(srcdir)/setup.py install --hostlist --root=$(top_srcdir)

idset:
	$(AM_V_GEN)$(PYTHON) $(srcdir)/setup.py install --idset --root=$(top_srcdir)

rlist:
	$(AM_V_GEN)$(PYTHON) $(srcdir)/setup.py install --rlist --root=$(top_srcdir)

security:
	$(AM_V_GEN)$(PYTHON) $(srcdir)/setup.py install --security \
	    --security-include=$(FLUX_SECURITY_INCDIR) \
	    --security-src=/code/security --root=$(top_srcdir)

.PHONY: all core hostlist idset rlist security

.PRECIOUS: Makefile

clean-local:
	-rm -f .coverage*
	


