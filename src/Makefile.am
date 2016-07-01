.NOTPARALLEL:

#This order is *important*, common must preceed modules, 
#		            modules must preceed lib 
SUBDIRS = common modules broker connectors bindings cmd test

check-local: all

noinst_HEADERS = \
	include/flux/core.h \
	include/flux/optparse.h
