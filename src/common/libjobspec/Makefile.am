if ENABLE_JOBSPEC
noinst_LTLIBRARIES = libjobspec.la

libjobspec_la_CXXFLAGS = \
	$(WARNING_CXXFLAGS) \
	$(CODE_COVERAGE_CXXFLAGS) \
	$(YAMLCPP_CFLAGS)
libjobspec_la_LIBADD = $(CODE_COVERAGE_LIBS) $(YAMLCPP_LIBS)
libjobspec_la_SOURCES = jobspec.cpp jobspec.hpp

endif
