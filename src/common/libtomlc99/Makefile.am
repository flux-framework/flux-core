AM_CFLAGS = \
	-I$(top_srcdir) \
	$(WARNING_CFLAGS) \
	-Wno-unused-parameter \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	$(CODE_COVERAGE_CPPFLAGS)

EXTRA_DIST = \
	LICENSE \
	README.md

noinst_LTLIBRARIES = libtomlc99.la

libtomlc99_la_SOURCES = \
	toml.c \
	toml.h

TESTS = test_toml.t

check_PROGRAMS = \
	toml_cat \
	toml_json \
	$(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh

test_cppflags = \
	-DTEST_GOOD_INPUT=\"$(srcdir)/BurntSushi_input/valid\" \
	-DTEST_BAD_INPUT=\"$(srcdir)/BurntSushi_input/invalid\" \
	$(AM_CPPFLAGS)

test_ldadd = \
	$(top_builddir)/src/common/libtomlc99/libtomlc99.la \
	$(top_builddir)/src/common/libtap/libtap.la

toml_cat_SOURCES = toml_cat.c
toml_cat_LDADD = $(test_ldadd)
toml_cat_CPPFLAGS = $(test_cppflags)

toml_json_SOURCES = toml_json.c
toml_json_LDADD = $(test_ldadd)
toml_json_CPPFLAGS = $(test_cppflags)

test_toml_t_SOURCES = test/toml.c
test_toml_t_LDADD = $(test_ldadd)
test_toml_t_CPPFLAGS = $(test_cppflags)

EXTRA_DIST += \
	BurntSushi_input/invalid/array-mixed-types-arrays-and-ints.toml \
	BurntSushi_input/invalid/array-mixed-types-ints-and-floats.toml \
	BurntSushi_input/invalid/array-mixed-types-strings-and-ints.toml \
	BurntSushi_input/invalid/datetime-malformed-no-leads.toml \
	BurntSushi_input/invalid/datetime-malformed-no-secs.toml \
	BurntSushi_input/invalid/datetime-malformed-no-t.toml \
	BurntSushi_input/invalid/datetime-malformed-with-milli.toml \
	BurntSushi_input/invalid/duplicate-keys.toml \
	BurntSushi_input/invalid/duplicate-key-table.toml \
	BurntSushi_input/invalid/duplicate-tables.toml \
	BurntSushi_input/invalid/empty-implicit-table.toml \
	BurntSushi_input/invalid/empty-table.toml \
	BurntSushi_input/invalid/float-no-leading-zero.toml \
	BurntSushi_input/invalid/float-no-trailing-digits.toml \
	BurntSushi_input/invalid/key-empty.toml \
	BurntSushi_input/invalid/key-hash.toml \
	BurntSushi_input/invalid/key-newline.toml \
	BurntSushi_input/invalid/key-open-bracket.toml \
	BurntSushi_input/invalid/key-single-open-bracket.toml \
	BurntSushi_input/invalid/key-space.toml \
	BurntSushi_input/invalid/key-start-bracket.toml \
	BurntSushi_input/invalid/key-two-equals.toml \
	BurntSushi_input/invalid/string-bad-byte-escape.toml \
	BurntSushi_input/invalid/string-bad-escape.toml \
	BurntSushi_input/invalid/string-byte-escapes.toml \
	BurntSushi_input/invalid/string-no-close.toml \
	BurntSushi_input/invalid/table-array-implicit.toml \
	BurntSushi_input/invalid/table-array-malformed-bracket.toml \
	BurntSushi_input/invalid/table-array-malformed-empty.toml \
	BurntSushi_input/invalid/table-empty.toml \
	BurntSushi_input/invalid/table-nested-brackets-close.toml \
	BurntSushi_input/invalid/table-nested-brackets-open.toml \
	BurntSushi_input/invalid/table-whitespace.toml \
	BurntSushi_input/invalid/table-with-pound.toml \
	BurntSushi_input/invalid/text-after-array-entries.toml \
	BurntSushi_input/invalid/text-after-integer.toml \
	BurntSushi_input/invalid/text-after-string.toml \
	BurntSushi_input/invalid/text-after-table.toml \
	BurntSushi_input/invalid/text-before-array-separator.toml \
	BurntSushi_input/invalid/text-in-array.toml \
	BurntSushi_input/valid/array-empty.toml \
	BurntSushi_input/valid/array-nospaces.toml \
	BurntSushi_input/valid/arrays-hetergeneous.toml \
	BurntSushi_input/valid/arrays-nested.toml \
	BurntSushi_input/valid/arrays.toml \
	BurntSushi_input/valid/bool.toml \
	BurntSushi_input/valid/comments-everywhere.toml \
	BurntSushi_input/valid/datetime.toml \
	BurntSushi_input/valid/empty.toml \
	BurntSushi_input/valid/example.toml \
	BurntSushi_input/valid/float.toml \
	BurntSushi_input/valid/implicit-and-explicit-after.toml \
	BurntSushi_input/valid/implicit-and-explicit-before.toml \
	BurntSushi_input/valid/implicit-groups.toml \
	BurntSushi_input/valid/integer.toml \
	BurntSushi_input/valid/key-equals-nospace.toml \
	BurntSushi_input/valid/key-space.toml \
	BurntSushi_input/valid/key-special-chars.toml \
	BurntSushi_input/valid/long-float.toml \
	BurntSushi_input/valid/long-integer.toml \
	BurntSushi_input/valid/multiline-string.toml \
	BurntSushi_input/valid/raw-multiline-string.toml \
	BurntSushi_input/valid/raw-string.toml \
	BurntSushi_input/valid/string-empty.toml \
	BurntSushi_input/valid/string-escapes.toml \
	BurntSushi_input/valid/string-simple.toml \
	BurntSushi_input/valid/string-with-pound.toml \
	BurntSushi_input/valid/table-array-implicit.toml \
	BurntSushi_input/valid/table-array-many.toml \
	BurntSushi_input/valid/table-array-nest.toml \
	BurntSushi_input/valid/table-array-one.toml \
	BurntSushi_input/valid/table-empty.toml \
	BurntSushi_input/valid/table-sub-empty.toml \
	BurntSushi_input/valid/table-whitespace.toml \
	BurntSushi_input/valid/table-with-pound.toml \
	BurntSushi_input/valid/unicode-escape.toml \
	BurntSushi_input/valid/unicode-literal.toml
