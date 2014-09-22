topdir     :=        $(shell cd ../../ && pwd)
builddir   :=        $(shell pwd)
libfluxapi :=        libfluxapi.so
programs   :=        test_sleeper test_launch_spawn

CFLAGS     +=       -ggdb -Wall -Werror \
                    -I./ -I$(topdir) -I$(topdir)/include -I$(topdir)/util
LDFLAGS    +=       -L$(topdir)/lib \
                    -L$(topdir)/util \
                    -L$(topdir)/zutil \
                    -L$(builddir)/ \
                    -Wl,-rpath=$(topdir)/lib \
                    -Wl,-rpath=$(builddir)

LIBADD     +=       $(topdir)/util/libutil.a -lcmb -lczmq
TSTLIBADD  +=       -lfluxapi 
CLEANFILES :=       $(libfluxapi) $(programs)

libfluxapi_SOURCES        := flux_api.c flux_api.h flux_lwj_desc.h
test_sleeper_SOURCES      := test_sleeper.c 
test_launch_spawn_SOURCES := test_launch_spawn.c

all: $(libfluxapi) $(programs) docu

libfluxapi.so : flux_api.o 
	$(CC) -shared $(LDFLAGS) -o $@ $^ $(LIBADD)

flux_api.o : $(libfluxapi_SOURCES)
	$(CC) $(CFLAGS) -fpic -c -o $@ $< 

test_sleeper: $(test_sleeper_SOURCES:.c=.o)
	$(CC) $(LDFLAGS) -o $@ $^ $(TSTLIBADD) $(LIBADD)

test_launch_spawn: $(test_launch_spawn_SOURCES:.c=.o)
	$(CC) $(LDFLAGS) -o $@ $^ $(TSTLIBADD) $(LIBADD)

docu: doc/doxy_conf.txt
	cd doc; doxygen doxy_conf.txt
        
clean:
	rm -rf *.o *~ $(CLEANFILES) doc/html doc/man doc/latex tags *breakpoint*

