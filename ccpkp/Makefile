# Comment/uncomment the following line to disable/enable debugging
DEBUG = n
ONE_PIPE = n

# Add your debugging flag (or not) to EXTRA_CFLAGS
ifeq ($(DEBUG),y)
  DEBFLAGS = -O1 -g -D__DEBUG__ # "-O" is needed to expand inlines
else
  DEBFLAGS = -Ofast
endif

ifeq ($(ONE_PIPE),y)
	DEBFLAGS += -DONE_PIPE
endif

test: lfq/lfq.c lfq/lfq.h lfq/multi-writer-test.c
	gcc lfq/lfq.c lfq/multi-writer-test.c $(DEBFLAGS) -lpthread -o ./lfq/multi-writer-test
	./lfq/multi-writer-test

clean:
	rm -rf *.o *~ ./lfq/multi-writer-test

