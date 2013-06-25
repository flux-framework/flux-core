
LIBS := 	/usr/local/tools/pmgr/lib/libpmgr_collective.a -llua
CFLAGS :=       -I/usr/local/tools/pmgr/include

OBJECTS :=     list.o log_msg.o pepe-lua.o pepe-launcher.o optparse.o
TARGET :=      pepe-launcher


all:  $(TARGET)
$(TARGET): $(OBJECTS)
	$(CC) -o $@ $^ $(LIBS)

.c.o:
	$(CC) -ggdb -Wall $(CFLAGS) $(EXTRA_CFLAGS) -o $@ -fPIC -c $<

clean:
	rm -f $(TARGET) $(OBJECTS)
