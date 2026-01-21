CC = gcc
CFLAGS = -Wall -Iinclude
LDFLAGS = -lpthread

SRC_TIMER = src/timer.c 
SRC_UE = src/ue.c 
SRC_GNB = src/gNodeB.c 
SRC_AMF = src/amf.c

all: ue gnb amf

ue:	$(SRC_TIMER) $(SRC_UE)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

gnb:$(SRC_TIMER) $(SRC_GNB)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

amf:$(SRC_AMF)
	$(CC) $(CFLAGS) $^ -o $@ 

clean:
	rm -f ue gnb amf