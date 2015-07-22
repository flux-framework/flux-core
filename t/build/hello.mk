include $(FLUX_MAKEFILE_INC)


ALL =  hello_czmq hello_jsonc hello_flux_core hello_flux_internal

nothing:
	@echo $(FLUX_MAKEFILE_INC)

all: $(ALL)

hello_czmq: hello_czmq.c
	$(CC) -o $@ $< $(FLUX_CFLAGS) $(FLUX_LIBS)

hello_jsonc: hello_jsonc.c
	$(CC) -o $@ $< $(FLUX_CFLAGS) $(FLUX_LIBS)

hello_flux_core: hello_flux_core.c
	$(CC) -o $@ $< $(FLUX_CFLAGS) $(FLUX_LIBS)

hello_flux_internal: hello_flux_internal.c
	$(CC) -o $@ $< $(FLUX_CFLAGS) $(FLUX_LIBS)

clean:
	rm -f $(ALL)
