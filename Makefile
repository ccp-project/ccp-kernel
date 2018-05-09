# Comment/uncomment the following line to disable/enable debugging
DEBUG = y
ONE_PIPE = n

# Add your debugging flag (or not) to EXTRA_CFLAGS
ifeq ($(DEBUG),y)
  DEBFLAGS = -O1 -g -DDEBUG_MODE # "-O" is needed to expand inlines
else
  DEBFLAGS = -Ofast
endif

ifeq ($(ONE_PIPE),y)
	DEBFLAGS += -DONE_PIPE
endif

EXTRA_CFLAGS += $(DEBFLAGS)
EXTRA_CFLAGS += -std=gnu99 -Wno-declaration-after-statement -fgnu89-inline


TARGET = ccp
ccp-objs := libccp/serialize.o libccp/ccp_priv.o libccp/machine.o libccp/ccp.o ccpkp/ccpkp.o ccpkp/lfq/lfq.o tcp_ccp.o ccp_nl.o

obj-m := $(TARGET).o

all:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(CURDIR) modules

clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(CURDIR) clean
