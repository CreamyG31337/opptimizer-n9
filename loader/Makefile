SHELL=/bin/sh
CFLAGS += -fstack-protector -Wall -std=c89 -fPIE
CPPFLAGS += -DNDEBUG -D_GNU_SOURCE -D_FORTIFY_SOURCE=2
LDFLAGS += -Wl,-z,relro,-z,now -fPIE -pie
LDLIBS += -lcreds
INSTALL=install
INSTALL_PROGRAM=$(INSTALL)

.PHONY: all install clean

all: oppldr

oppldr: oppldr.o

oppldr.c: modhash.inc

modhash.inc: ../symsearch/symsearch.ko ../opptimizer/opptimizer.ko
	./hashmod.sh

install: oppldr
	$(INSTALL_PROGRAM) -D -m 0755 oppldr "$(DESTDIR)/opt/opptimizer/bin/oppldr"

clean:
	rm -f oppldr oppldr.o modhash.inc
