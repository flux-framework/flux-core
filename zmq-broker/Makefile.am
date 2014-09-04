SUBDIRS = util zutil . 

AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	-DPLUGIN_PATH=\"$(top_builddir)/zmq-broker/plugins\" \
	-DEXEC_PATH=\"$(top_builddir)/zmq-broker/libexec\" \
	-Iutil -Izutil -Iinclude

bin_PROGRAMS = \
	cmbd

noinst_LIBRARIES = \
	libcmb.a

libcmb_a_SOURCES = \
	kvscli.c \
	barriercli.c \
	logcli.c \
	cmbdcli.c \
	mrpc.c \
	handle.c \
        waitqueue.c \
	security.c \
	reduce.c \
	modctlcli.c \
        livecli.c

cmbd_LDADD = \
	$(top_builddir)/zmq-broker/util/libutil.a \
	$(top_builddir)/zmq-broker/libcmb.a \
	$(LIBMUNGE) $(LIBJSON) $(LIBZMQ) $(LIBCZMQ) -ldl
	
cmbd_SOURCES = \
	cmbd.c \
	plugin.c \
	pmiwrap.c
