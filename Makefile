# Makefile: origo - minimal supervised init
.POSIX:

include config.mk

SRC = origo.c serva.c svc.c slog.c util.c
OBJ = $(SRC:.c=.o)
BIN = origo serva svc slog

all: $(BIN)

origo: origo.o util.o
	$(CC) $(LDFLAGS) -o $@ origo.o util.o

serva: serva.o util.o
	$(CC) $(LDFLAGS) -o $@ serva.o util.o

svc: svc.o util.o
	$(CC) $(LDFLAGS) -o $@ svc.o util.o

slog: slog.o util.o
	$(CC) $(LDFLAGS) -o $@ slog.o util.o

%.o: %.c util.h arg.h
	$(CC) $(CFLAGS) -c -o $@ $<

install: all
	install -Dm755 origo             $(DESTDIR)$(PREFIX)$(SBIN_DIR)/origo
	install -Dm755 serva             $(DESTDIR)$(PREFIX)$(SBIN_DIR)/serva
	install -Dm755 svc               $(DESTDIR)$(PREFIX)$(BIN_DIR)/svc
	install -Dm755 slog              $(DESTDIR)$(PREFIX)$(BIN_DIR)/slog
	install -d                       $(DESTDIR)/etc/ssv/boot
	install -d                       $(DESTDIR)/etc/ssv/default

install-files:
	install -Dm755 files/rc.boot      $(DESTDIR)/etc/rc.boot
	install -Dm755 files/rc.local     $(DESTDIR)/etc/rc.local
	install -Dm755 files/rc.shutdown  $(DESTDIR)/etc/rc.shutdown
	install -Dm755 files/rc.single    $(DESTDIR)/etc/rc.single
	install -Dm755 files/bin/poweroff $(DESTDIR)$(PREFIX)/bin/poweroff
	install -Dm755 files/bin/reboot   $(DESTDIR)$(PREFIX)/bin/reboot

clean:
	rm -f $(BIN) $(OBJ)

.PHONY: all install install-files clean

