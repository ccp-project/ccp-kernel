TARGET = ccp
ccp-objs := libccp/serialize.o libccp/send_machine.o libccp/measurement_machine.o libccp/ccp.o tcp_ccp.o ccp_nl.o

TESTTARGET = ccptest
ccptest-objs := netlink_test.o serialize.o

obj-m := $(TARGET).o $(TESTTARGET).o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
