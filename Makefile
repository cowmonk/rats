# Makefile: origo - minimal supervised init
.POSIX:

include config.mk

SRC = origo.c serva.c svc.c slog.c util.c
OBJ = $(SRC:.c=.o)
BIN = origo serva svc slog

all: $(BIN)

origo: origo.o util.o
	$(CC) $(LDFLAGS) -o $@ origo.o util.o $(LDLIBS)

serva: serva.o util.o
	$(CC) $(LDFLAGS) -o $@ serva.o util.o $(LDLIBS)

svc: svc.o util.o
	$(CC) $(LDFLAGS) -o $@ svc.o util.o $(LDLIBS)

slog: slog.o util.o
	$(CC) $(LDFLAGS) -o $@ slog.o util.o $(LDLIBS)

$(OBJ): util.h arg.h

.c.o:
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c99 -c -o $@ $<

install: all
	mkdir -p "$(DESTDIR)$(PREFIX)$(SBIN_DIR)" "$(DESTDIR)$(PREFIX)$(BIN_DIR)"
	cp -f origo serva "$(DESTDIR)$(PREFIX)$(SBIN_DIR)/"
	cp -f svc slog "$(DESTDIR)$(PREFIX)$(BIN_DIR)/"
	chmod 755 "$(DESTDIR)$(PREFIX)$(SBIN_DIR)/origo" "$(DESTDIR)$(PREFIX)$(SBIN_DIR)/serva"
	chmod 755 "$(DESTDIR)$(PREFIX)$(BIN_DIR)/svc" "$(DESTDIR)$(PREFIX)$(BIN_DIR)/slog"
	mkdir -p "$(DESTDIR)/etc/ssv/boot" "$(DESTDIR)/etc/ssv/default"

install-files:
	mkdir -p "$(DESTDIR)/etc" "$(DESTDIR)$(PREFIX)$(BIN_DIR)"
	cp -f files/rc.boot files/rc.local files/rc.shutdown files/rc.single "$(DESTDIR)/etc/"
	chmod 755 "$(DESTDIR)/etc/rc.boot" "$(DESTDIR)/etc/rc.local" "$(DESTDIR)/etc/rc.shutdown" "$(DESTDIR)/etc/rc.single"
	cp -f files/bin/poweroff files/bin/reboot "$(DESTDIR)$(PREFIX)$(BIN_DIR)/"
	chmod 755 "$(DESTDIR)$(PREFIX)$(BIN_DIR)/poweroff" "$(DESTDIR)$(PREFIX)$(BIN_DIR)/reboot"

check:
	cd tests && $(MAKE) check

clean:
	rm -f $(BIN) $(OBJ)
	cd tests && $(MAKE) clean
