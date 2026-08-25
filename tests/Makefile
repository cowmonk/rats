# tests/Makefile: build serva/svc against a sandboxed ssv dir + socket
.POSIX:

include ../config.mk

TOP = ..
CC ?= cc

all: serva-test svc-test

serva-test: $(TOP)/serva.c $(TOP)/util.o
	$(CC) $(CFLAGS) -DSVC_DIR='"$(CURDIR)/ssv"' \
	    -DSOCK_PATH='"$(CURDIR)/serva.sock"' \
	    $(LDFLAGS) -o $@ $(TOP)/serva.c $(TOP)/util.o

svc-test: $(TOP)/svc.c $(TOP)/util.o
	$(CC) $(CFLAGS) -DSOCK_PATH='"$(CURDIR)/serva.sock"' \
	    $(LDFLAGS) -o $@ $(TOP)/svc.c $(TOP)/util.o

$(TOP)/util.o: $(TOP)/util.c $(TOP)/util.h
	cd $(TOP) && $(MAKE) util.o

clean:
	rm -f serva-test svc-test serva.log

.PHONY: all clean run-tests
