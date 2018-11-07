dist_noinst_SCRIPTS = spellcheck

EXTRA_DIST = spell.en.pws

AM_TESTS_ENVIRONMENT = \
	ASPELL=$(ASPELL) \
	pws_dict=$(abs_top_srcdir)/doc/test/spell.en.pws \
	man_base_dir=$(abs_top_srcdir)/doc

LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh

TESTS = spellcheck
