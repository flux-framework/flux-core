.NOTPARALLEL:

SUBDIRS = src doc etc t

EXTRA_DIST = \
	config/tap-driver.sh \
	META \
	DISCLAIMER.LLNS \
	README.md

ACLOCAL_AMFLAGS = -I config

# coverage

CODE_COVERAGE_IGNORE_PATTERN = \
	"$(abs_top_builddir)/t/*" \
	"/test/*.c" \
	"/tests/*.c" \
	"man3/*.c" \
	"libtap/*" \
	"libjsonc/*" \
	"libev/*" \
	"/usr/*" \
	"bindings/python/*" \
	"common/liblsd/*" \
	"common/libutil/sds.[ch]"
CODE_COVERAGE_LCOV_OPTIONS =
@CODE_COVERAGE_RULES@

# Remove other coverage files on clean:
clean:
	-rm $(CODE_COVERAGE_OUTPUT_FILE).initial
	-find . -name *.gcno -delete
