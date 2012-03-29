KERNEL_BUILD := /usr/src/kernel-headers
EXTRA_CFLAGS :=
FILENAME := symsearch

obj-m += $(FILENAME).o
symsearch-y := search.o hijack.o

all:
	make -C $(KERNEL_BUILD) ARCH=arm M=$(PWD) modules
	strip --strip-debug $(FILENAME).ko

clean:
	make -C $(KERNEL_BUILD) M=$(PWD) clean 2> /dev/null
	rm -f modules.order *~
