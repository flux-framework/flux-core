.NOTPARALLEL:

#This order is *important*, common must precede modules,
#		            modules must precede lib
SUBDIRS = common modules broker connectors bindings shell cmd test

check-local: all

noinst_HEADERS = \
	include/flux/core.h \
	include/flux/optparse.h \
	include/flux/idset.h \
	include/flux/schedutil.h \
	include/flux/shell.h \
	include/flux/hostlist.h \
	include/flux/jobtap.h \
	include/flux/taskmap.h
