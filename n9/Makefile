KERNEL_BUILD := /usr/src/kernel-headers
EXTRA_CFLAGS :=
FILENAME := opptimizer

obj-m += $(FILENAME).o

all:
	make -C $(KERNEL_BUILD) ARCH=arm M=$(PWD) \
	KBUILD_EXTRA_SYMBOLS=/home/lance/opptimizer1/symsearch/n9/Module.symvers \
	modules
	strip --strip-debug $(FILENAME).ko
	mv $(FILENAME).ko $(FILENAME)_n9.ko

clean:
	make -C $(KERNEL_BUILD) M=$(PWD) clean 2> /dev/null
	rm -f modules.order *~
