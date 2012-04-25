SHELL=/bin/sh
obj-m := opptimizer.o
KBUILD_EXTRA_SYMBOLS += "$(PWD)/../symsearch/Module.symvers"
KDIR := /usr/src/kernel-headers
INSTALL=install
INSTALL_PROGRAM=$(INSTALL)

.PHONY: all clean

all: opptimizer.ko

opptimizer.ko: opptimizer.c opp_info.h ../symsearch/Module.symvers
	$(MAKE) -C "$(KDIR)" M="$(PWD)"

install: opptimizer.ko
	$(INSTALL_PROGRAM) -D -m 0644 opptimizer.ko "$(DESTDIR)/lib/modules/2.6.32.48-dfl61-20115101/opptimizer.ko"

clean:
	$(MAKE) -C "$(KDIR)" M="$(PWD)" clean
