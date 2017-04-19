TARGET = ccp
ccp-objs := tcp_ccp.o ccp_nl.o serialize.o 

TESTTARGET = ccptest
ccptest-objs := netlink.o serialize.o

obj-m := $(TARGET).o $(TESTTARGET).o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
