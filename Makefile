
DEBUG = n
ONE_PIPE = n
IPC = 0 # 0 == netlink, 1 == chardev

# Add your debugging flag (or not) to EXTRA_CFLAGS
ifeq ($(DEBUG),y)
  DEBFLAGS = -O1 -g -D__DEBUG__ -D__LOG_DEBUG__ # "-O" is needed to expand inlines
else
  DEBFLAGS = -Ofast
endif

KERNEL_VERSION_MAJOR := $(shell uname -r | awk -F '.' '{print $$1}')
KERNEL_VERSION_MINOR := $(shell uname -r | awk -F '.' '{print $$2}')
EXTRA_CFLAGS += -D__KERNEL_VERSION_MAJOR__=$(KERNEL_VERSION_MAJOR) 
EXTRA_CFLAGS += -D__KERNEL_VERSION_MINOR__=$(KERNEL_VERSION_MINOR)
EXTRA_CFLAGS += -D__IPC__=$(IPC)

ifeq ($(ONE_PIPE),y)
	DEBFLAGS += -DONE_PIPE
endif

EXTRA_CFLAGS += $(DEBFLAGS)
EXTRA_CFLAGS += -std=gnu99 -Wno-declaration-after-statement -fgnu89-inline -D__KERNEL__

TARGET = ccp
ccp-objs := libccp/serialize.o libccp/ccp_priv.o libccp/machine.o libccp/ccp.o ccpkp/ccpkp.o ccpkp/lfq/lfq.o tcp_ccp.o ccp_nl.o

obj-m := $(TARGET).o

all:
ifneq ($(shell expr $(KERNEL_VERSION_MAJOR) \>= 4),1)
	$(error "Only kernel version >= 4 is supported: $(KERNEL_VERSION_MAJOR) $(KERNEL_VERSION_MINOR)")
endif 
ifeq ($(shell expr $(KERNEL_VERSION_MINOR) \>= 4),1)
endif
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(CURDIR) modules

clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(CURDIR) clean
