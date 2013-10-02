topdir  :=          $(shell cd .. && pwd)
builddir  :=        $(shell pwd)
programs =          wrexecd wreck
plugins  =          jobsrv.o wrexecsrv.o

CFLAGS +=           -ggdb -Wall -Werror \
                    -I$(topdir) -I$(topdir)/include -I$(topdir)/util
LDFLAGS +=          -L$(topdir)/lib \
		    -L$(topdir)/util \
		    -L$(topdir)/zutil \
		    -Wl,-rpath=$(topdir)/lib
		    
LIBADD +=           $(topdir)/util/libutil.a -lcmb -lczmq
CLEANFILES :=       $(programs) rexec-config.h

wrexecd_SOURCES :=  wrexecd.c
wreck_SOURCES :=    wreck.c

all:                rexec-config.h $(programs) $(plugins) $(tests)

rexec-config.h :
	@(echo '#define REXECD_PATH "$(builddir)/wrexecd"') > $@

wrexecd : $(wrexecd_SOURCES:.c=.o)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBADD)

wreck : $(wreck_SOURCES:.c=.o)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBADD)
	

clean:
	rm -f *.o $(CLEANFILES) tags

