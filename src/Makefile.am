.NOTPARALLEL:

#This order is *important*, common must precede modules,
#		            modules must precede lib
SUBDIRS = common modules broker shell connectors bindings cmd test

check-local: all

noinst_HEADERS = \
	include/flux/core.h \
	include/flux/optparse.h \
	include/flux/idset.h
