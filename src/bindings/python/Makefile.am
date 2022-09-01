if HAVE_FLUX_SECURITY
all: core.so-built hostlist.so-built idset.so-built rlist.so-built security.so-built
else
all: core.so-built hostlist.so-built idset.so-built rlist.so-built
endif

core.so-built: Makefile
	$(AM_V_GEN)$(PYTHON) $(srcdir)/setup.py install --flux-root=$(top_srcdir)
	touch core.so-built

hostlist.so-built:
	$(AM_V_GEN)$(PYTHON) $(srcdir)/setup.py install --hostlist --flux-root=$(top_srcdir)
	touch hostlist.so-built

idset.so-built:
	$(AM_V_GEN)$(PYTHON) $(srcdir)/setup.py install --idset --flux-root=$(top_srcdir)
	touch idset.so-built

rlist.so-built:
	$(AM_V_GEN)$(PYTHON) $(srcdir)/setup.py install --rlist --flux-root=$(top_srcdir)
	touch rlist.so-built

security.so-built:
	$(AM_V_GEN)$(PYTHON) $(srcdir)/setup.py install --security \
	    --security-include=$(FLUX_SECURITY_INCDIR) \
	    --security-src=/code/security --flux-root=$(top_srcdir)
	touch security.so-built

.PHONY: all core.so-built core.so-built idset.so-built rlist.so-built security.so-built

.PRECIOUS: Makefile

clean:
	rm -rf hostlist.so-built core.so-built idset.so-built rlist.so-built security.so-built

clean-local:
	-rm -f .coverage*
