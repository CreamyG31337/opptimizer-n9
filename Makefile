SUBDIRS = n9
.PHONY: $(SUBDIRS)

all:	$(SUBDIRS)

n9:
	cd n9 && make

clean_n9:
	cd n9 && make clean

clean:	clean_n9
	rm -f *~

dist:
	tar cvfz ../opptimizer.tar.gz ../opptimizer/
