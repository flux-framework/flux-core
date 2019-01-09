if ENABLE_JOBSPEC
noinst_LTLIBRARIES = libjobspec.la

libjobspec_la_CXXFLAGS = \
	$(WARNING_CXXFLAGS) \
	$(CODE_COVERAGE_CXXFLAGS) \
	$(YAMLCPP_CFLAGS)
libjobspec_la_LIBADD = $(CODE_COVERAGE_LIBS) $(YAMLCPP_LIBS)
libjobspec_la_SOURCES = jobspec.cpp jobspec.hpp

check_PROGRAMS = test_validate

test_validate_SOURCES = test/validate.cpp
test_validate_LDADD = \
	$(builddir)/libjobspec.la \
	$(YAMLCPP_LIBS)

endif
