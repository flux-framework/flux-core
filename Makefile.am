.NOTPARALLEL:

SUBDIRS = . src doc etc t

EXTRA_DIST = \
	config/tap-driver.sh \
	config/tap-driver.py \
	NOTICE.LLNS \
	LICENSE \
	README.md \
	vscode.md \
	NEWS.md \
	CONTRIBUTING.md \
	MAINTAINERS.md \
	scripts/requirements-dev.txt \
	scripts/install-deps-deb.sh \
	scripts/install-deps-rpm.sh \
	scripts/install-deps-macos.sh \
	scripts/configure-macos.sh \
	scripts/check-macos.sh

ACLOCAL_AMFLAGS = -I config

# coverage

CODE_COVERAGE_IGNORE_PATTERN = \
	"$(abs_top_builddir)/t/*" \
	"*/test/*.c" \
	"*/tests/*.c" \
	"*/man3/*.c" \
	"*/libtap/*" \
	"*/libev/*" \
	"/usr/include/*" \
	"/usr/lib*" \
	"*/bindings/python/*" \
	"*/common/liblsd/*" \
	"*/common/liboptparse/getopt*" \
	"*/common/libtestutil/*" \
	"*/common/libyuarel/*" \
	"*/common/libczmqcontainers/*" \
	"*/common/libccan/*" \
	"*/common/libsqlite3/*" \
	"*/common/libmissing/*"

# ignore lcov errors to avoid merge mismatch issue since lcov < 2 doesn't offer
# an option to just ignore this error, we use this env var to ignore all, see:
# https://github.com/flux-framework/flux-core/issues/6078
export GCOV_ERROR_FILE=/dev/null
CODE_COVERAGE_LCOV_OPTIONS =
@CODE_COVERAGE_RULES@

# Many of flux-core's tests live down in subdirectories with
# the core that that it is testing.  However, some of those tests
# also have dependencies on other subdirectories higher up the
# source tree.  With the recursive Makefiles approach, there is
# no easy way to express that build dependency in a way that will
# actually trigger the build of the that dependency.  The following
# check-local rule, in conjunction with putting "." _first_ in this
# file's SUBDIRS, ensures that "all" is built before any of the
# recursive checks.
check-local: all

export DEB_BUILD_OPTIONS ?= nocheck terse
deb: debian scripts/debbuild.sh
	+@$(top_srcdir)/scripts/debbuild.sh $(abs_top_srcdir)

clean-local:
	@rm -rf debbuild
