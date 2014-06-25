PMGRDIR = ../foreign/pmgr_collective/pmgr_collective-1.0_7

LIBS := 	$(PMGRDIR)/lib/libpmgr_collective.a -llua -lpmi
CFLAGS :=       -I$(PMGRDIR)/include

OBJECTS :=     list.o log_msg.o pepe-lua.o pepe-launcher.o optparse.o
TARGET :=      pepe-launcher


all:  $(TARGET)
$(TARGET): $(OBJECTS)
	$(CC) -o $@ $^ $(LIBS)

.c.o:
	$(CC) -ggdb -Wall $(CFLAGS) $(EXTRA_CFLAGS) -o $@ -fPIC -c $<

clean:
	rm -f $(TARGET) $(OBJECTS)
