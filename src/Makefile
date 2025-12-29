DIRS = pdu_lib

#CROSS_COMPILE=mips-openwrt-linux-
#CC = $(CROSS_COMPILE)gcc
#STRIP = $(CROSS_COMPILE)strip
#CFLAGS = -O2
EXE = bridge_traffic_monitor

OBJS = pdu_lib/pdu.o pdu_lib/ucs2_to_utf8.o

all: $(EXE)

$(EXE): bridge_traffic_monitor.o pdu_lib
	$(CC) $(CFLAGS) bridge_traffic_monitor.o $(OBJS) -o $(EXE)

bridge_traffic_monitor.o:
	$(CC) $(CFLAGS) bridge_traffic_monitor.c -c
	
pdu_lib: force_look
	cd pdu_lib; CROSS_COMPILE=$(CROSS_COMPILE) $(MAKE) $(MFLAGS)
	
clean:
	rm -rf *.o bridge_traffic_monitor
	for d in $(DIRS); do (cd $$d; $(MAKE) clean); done

strip:
	$(STRIP) -s bridge_traffic_monitor

force_look:
	true
	
