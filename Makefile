.PHONY: all clean install

all clean install:
	cd symsearch && $(MAKE) $@
	cd opptimizer && $(MAKE) $@
	cd loader && $(MAKE) $@
