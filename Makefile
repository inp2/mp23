LDFLAGS = -O3 -DNDEBUG
#-DFIXEDWIN=64
LD = gcc
CC = gcc

.PHONY: all clean

all: reliable_sender reliable_receiver

reliable_sender: sender_main.o
	$(LD) -o $@ $^

reliable_receiver: receiver_main.o
	$(LD) -o $@ $^

sender_main.o: sender_main.c common.h

receiver_main.o: receiver_main.c common.h

clean:
	rm -f *.o reliable_sender reliable_receiver
