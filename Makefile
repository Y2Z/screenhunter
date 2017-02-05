# screenhunter - automated cursor positioning and clicking tool for X11
# See LICENSE file for copyright and license details.

CFLAGS = -std=c99 -s -pedantic -Wall -Wextra -Wfatal-errors -pedantic-errors -O3 -D_XOPEN_SOURCE=500 -D_POSIX_C_SOURCE=200809L
CC     = gcc $(CFLAGS)

LIBS = -lX11 -lpng
PROG = screenhunter

all: $(PROG)

$(PROG):
	    $(CC) -o $(PROG) $(PROG).c $(LIBS)

clean:
	    rm -f $(PROG)

install: all
	@echo installing executable file to ${DESTDIR}${PREFIX}/bin
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	@cp -f $(PROG) ${DESTDIR}${PREFIX}/bin
	@chmod 755 ${DESTDIR}${PREFIX}/bin/$(PROG)

uninstall:
	@echo removing executable file from ${DESTDIR}${PREFIX}/bin
	@rm -f ${DESTDIR}${PREFIX}/bin/$(PROG)

.PHONY: all clean install uninstall
