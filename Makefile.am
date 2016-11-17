.NOTPARALLEL:

SUBDIRS = po src doc etc t

EXTRA_DIST = config/config.rpath  \
	src/include/gettext.h \
	config/tap-driver.sh \
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
	"common/libutil/sds.*" \
	"common/libminilzo/*" \
	"common/libjson-c/*" \
	"common/liboptparse/getopt*"

CODE_COVERAGE_LCOV_OPTIONS =
@CODE_COVERAGE_RULES@
