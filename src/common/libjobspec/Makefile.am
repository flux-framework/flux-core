if ENABLE_JOBSPEC
noinst_LTLIBRARIES = libjobspec.la

libjobspec_la_CFLAGS = ${YAMLCPP_CFLAGS}
libjobspec_la_LIBS = ${YAMLCPP_LIBS}
libjobspec_la_SOURCES = jobspec.cpp jobspec.hpp

fluxinclude_HEADERS = jobspec.hpp

if WITH_PKG_CONFIG
pkgconfig_DATA = flux-jobspec.pc
endif
endif
