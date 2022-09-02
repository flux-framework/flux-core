if HAVE_FLUX_SECURITY
all: core.so-built hostlist.so-built idset.so-built rlist.so-built security.so-built
else
all: core.so-built hostlist.so-built idset.so-built rlist.so-built
endif

# Note that if you provide --flux-root here it doesn't seem to take...
core.so-built: Makefile
	export FLUX_INSTALL_ROOT=$(realpath $(top_srcdir))
	$(AM_V_GEN)$(PYTHON) $(srcdir)/setup.py install
	touch core.so-built

hostlist.so-built:
	export FLUX_INSTALL_ROOT=$(realpath $(top_srcdir))
	$(AM_V_GEN)$(PYTHON) $(srcdir)/setup.py install --hostlist
	touch hostlist.so-built

idset.so-built:
	export FLUX_INSTALL_ROOT=$(realpath $(top_srcdir))
	$(AM_V_GEN)$(PYTHON) $(srcdir)/setup.py install --idset
	touch idset.so-built

rlist.so-built:
	export FLUX_INSTALL_ROOT=$(realpath $(top_srcdir))
	$(AM_V_GEN)$(PYTHON) $(srcdir)/setup.py install --rlist
	touch rlist.so-built

security.so-built:
	export FLUX_INSTALL_ROOT=$(realpath $(top_srcdir))
	$(AM_V_GEN)$(PYTHON) $(srcdir)/setup.py install --security \
	    --security-include=$(FLUX_SECURITY_INCDIR) \
	    --security-src=/code/security --flux-root=${FLUX_INSTALL_ROOT}
	touch security.so-built

.PHONY: all core.so-built core.so-built idset.so-built rlist.so-built security.so-built

.PRECIOUS: Makefile

clean:
	rm -rf hostlist.so-built core.so-built idset.so-built rlist.so-built security.so-built

clean-local:
	-rm -f .coverage*
