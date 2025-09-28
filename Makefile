# Userspace program
CC = gcc
CFLAGS = -O2 -Wall

all: netmon netmon_proc.ko

netmon: netmon.c
    $(CC) $(CFLAGS) -o netmon netmon.c

# Kernel module
obj-m += netmon_proc.o

netmon_proc.ko: netmon_proc.c
    make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
    rm -f netmon
    make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
    rm -f *.mod.c *.o *.order *.symvers *.ko *.cmd

.PHONY: all clean