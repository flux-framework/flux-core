TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh

TESTS = \
	t0000-sharness.t \
	t0001-basic.t \
	t1000-kvs-basic.t \
	t1001-barrier-basic.t

EXTRA_DIST= \
	$(TESTS) \
	aggregate-results.sh \
	sharness.sh \
	sharness.d \
	$(T)

clean-local:
	rm -fr trash-directory.* test-results .prove
