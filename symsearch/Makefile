SHELL=/bin/sh
obj-m := symsearch.o
symsearch-y := search.o hijack.o
KDIR := /usr/src/kernel-headers
INSTALL=install
INSTALL_PROGRAM=$(INSTALL)

.PHONY: all install clean

all: symsearch.ko

symsearch.ko: search.c hijack.c symsearch.h
	$(MAKE) -C "$(KDIR)" M="$(PWD)"

install: symsearch.ko
	$(INSTALL_PROGRAM) -D -m 0644 symsearch.ko "$(DESTDIR)/lib/modules/2.6.32.48-dfl61-20115101/symsearch.ko"	

clean:
	$(MAKE) -C "$(KDIR)" M="$(PWD)" clean
