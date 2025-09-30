# Userspace program
CC = gcc
CFLAGS = -O2 -Wall
ARCH ?= arm64
CROSS_COMPILE ?= aarch64-linux-gnu-
KERNEL_DIR ?= /lib/modules/$(shell uname -r)/build

all: netmon netmon_proc.ko

netmon: netmon.c
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -o netmon netmon.c

# Kernel module
obj-m += netmon_proc.o

netmon_proc.ko: netmon_proc.c
	@if [ -f "$(KERNEL_DIR)/Makefile" ]; then \
		echo "Building kernel module with KERNEL_DIR=$(KERNEL_DIR)"; \
		$(MAKE) -C $(KERNEL_DIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules; \
	else \
		echo "Error: Kernel Makefile not found at $(KERNEL_DIR)/Makefile"; \
		echo "Searching for kernel source directories:"; \
		find /tmp/openwrt/build_dir -name "linux-*" -type d 2>/dev/null | head -5 || echo "No OpenWrt build dirs found"; \
		exit 1; \
	fi

modules: netmon_proc.ko

clean:
	rm -f netmon netmon-arm64
	@if [ -f "$(KERNEL_DIR)/Makefile" ]; then \
		echo "Cleaning kernel module with KERNEL_DIR=$(KERNEL_DIR)"; \
		$(MAKE) -C $(KERNEL_DIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean; \
	else \
		echo "Kernel Makefile not found, doing manual clean"; \
	fi
	rm -f *.mod.c *.o *.order *.symvers *.ko *.cmd modules.builtin* Module.symvers .*.cmd
	rm -rf .tmp_versions

.PHONY: all clean modules