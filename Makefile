TARGET = ccp
ccp-objs := tcp_ccp.o fold_primitives.o ccp_nl.o stateMachine.o serialize.o 

TESTTARGET = ccptest
ccptest-objs := netlink_test.o serialize.o

obj-m := $(TARGET).o $(TESTTARGET).o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
