topdir  :=          $(shell cd .. && pwd)
builddir  :=        $(shell pwd)
programs =          wrexecd
plugins  =          jobsrv.o resrcsrv.o schedsrv.o schedplugin1.o wrexecsrv.o

CFLAGS +=           -ggdb -Wall -Werror \
                    -I$(topdir) -I$(topdir)/include -I$(topdir)/util
LDFLAGS +=          -L$(topdir)/lib \
		    -L$(topdir)/util \
		    -L$(topdir)/zutil \
		    -Wl,-rpath=$(topdir)/lib

LIBADD +=           $(topdir)/util/libutil.a $(topdir)/zutil/libzutil.a -lcmb -lczmq -ljson -lzmq
CLEANFILES :=       $(programs) rexec-config.h

wrexecd_SOURCES :=  wrexecd.c luastack.c

all:                rexec-config.h $(programs) $(plugins) $(tests)

rexec-config.h :
	@(echo '#define REXECD_PATH "$(builddir)/wrexecd"') > $@
	@(echo '#define REXECD_LUA_PATTERN  "$(builddir)/lua.d/*.lua"') >> $@

wrexecd : $(wrexecd_SOURCES:.c=.o)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBADD) $(topdir)/dlua/libdlua.a $(LUA_LIBS) -lm

clean:
	rm -f *.o $(CLEANFILES) tags

