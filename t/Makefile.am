TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh

TESTS = \
	t0000-sharness.t \
	t0001-basic.t \
	t1000-kvs-basic.t \
	t1001-barrier-basic.t \
	t2000-wreck.t \
	lua/t0001-send-recv.t \
	lua/t0002-rpc.t \
	lua/t0003-events.t \
	lua/t0007-alarm.t \
	lua/t1000-reactor.t \
	lua/t1001-timeouts.t \
	lua/t1002-kvs.t \
	lua/t1003-iowatcher.t

EXTRA_DIST= \
	$(TESTS) \
	aggregate-results.sh \
	sharness.sh \
	sharness.d \
	$(T)

clean-local:
	rm -fr trash-directory.* test-results .prove
