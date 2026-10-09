# tests/Makefile: build serva/svc against a sandboxed ssv dir + socket
.POSIX:

include ../config.mk

TOP = ..
TEST_DIR = sandbox

all: $(TEST_DIR)/serva-test $(TEST_DIR)/svc-test $(TEST_DIR)/control-test

$(TEST_DIR)/serva-test: $(TOP)/serva.c $(TOP)/util.c $(TOP)/util.h Makefile $(TOP)/config.mk
	mkdir -p "$(TEST_DIR)"
	$(CC) $(CPPFLAGS) $(CFLAGS) -DSVC_DIR='"$(TEST_DIR)/ssv"' \
	    -DSOCK_PATH='"$(TEST_DIR)/serva.sock"' \
	    $(LDFLAGS) -o $@ $(TOP)/serva.c $(TOP)/util.c $(LDLIBS)

$(TEST_DIR)/svc-test: $(TOP)/svc.c $(TOP)/util.c $(TOP)/util.h $(TOP)/arg.h Makefile $(TOP)/config.mk
	mkdir -p "$(TEST_DIR)"
	$(CC) $(CPPFLAGS) $(CFLAGS) -DSOCK_PATH='"$(TEST_DIR)/serva.sock"' \
	    $(LDFLAGS) -o $@ $(TOP)/svc.c $(TOP)/util.c $(LDLIBS)

$(TEST_DIR)/control-test: control.c Makefile $(TOP)/config.mk
	mkdir -p "$(TEST_DIR)"
	$(CC) $(CPPFLAGS) $(CFLAGS) -DSOCK_PATH='"$(TEST_DIR)/serva.sock"' \
	    $(LDFLAGS) -o $@ control.c $(LDLIBS)

check:
	MAKE='$(MAKE)' ./run.sh

clean:
	rm -f serva-test svc-test serva.log
	rm -f sandbox/serva-test sandbox/svc-test sandbox/control-test
	rm -f sandbox/*/serva-test sandbox/*/svc-test sandbox/*/control-test
