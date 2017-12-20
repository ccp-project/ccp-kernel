TARGET = ccp
ccp-objs := libccp/serialize.o libccp/ccp_priv.o libccp/send_machine.o libccp/measurement_machine.o libccp/ccp.o tcp_ccp.o ccp_nl.o

obj-m := $(TARGET).o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
