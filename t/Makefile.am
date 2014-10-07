T = $(wildcard $(srcdir)/t[0-9]*.t)
TESTS = runtests.sh

EXTRA_DIST= \
	$(TESTS) \
	aggregate-results.sh \
	sharness.sh \
	sharness.d \
	$(T)

clean-local:
	rm -fr trash-directory.* test-results .prove
