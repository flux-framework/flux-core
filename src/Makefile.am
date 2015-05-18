#This order is *important*, common must preceed modules, 
#		            modules must preceed lib 
SUBDIRS = common modules lib broker connectors bindings cmd test

modules: common
lib: modules

noinst_HEADERS = include/flux/core.h
