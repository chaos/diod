SYSNAME:=${shell uname}
SYSNAME!=uname
CFLAGS=-Wall -g -I ../include -DSYSNAME=$(SYSNAME)
LFLAGS=-lpthread -L../libnpfs -lnpfs
GPHOTO_HPATH=/usr/include/gphoto2
GPHOTO_LPATH=/usr/lib
ETPAN_HPATH=/usr/include/libetpan
ETPAN_LPATH=/usr/lib
LIBDIRS:=/usr/lib/ /usr/local/lib/ /opt/lib/

#ifeq ($(SYSNAME),Linux)
#NPFS_LFLAGS=-laio
#endif

FS=\
	npfs\
	ramfs\
	nullfs\
	ramfs2\
	echofs\

ifeq ($(SYSNAME),Linux)
	RDMADEPS:=\
		$(and $(foreach dir,$(LIBDIRS),$(wildcard $(dir)/libibverbs.*)), \
		$(foreach dir,$(LIBDIRS), $(wildcard $(dir)/librdmacm.*)))
	ifneq ($(strip $(RDMADEPS)),)
		FS += rdmanpfs
       endif
endif

.PHONY: all clean

all: $(FS)

npfs: ufs.o npfs.o
	$(CC) -o npfs $(CFLAGS) ufs.o npfs.o $(LFLAGS) $(NPFS_LFLAGS)

fus: fus.o
	$(CC) -o fus $(CFLAGS) fus.o $(LFLAGS)

rdmanpfs: ufs.o rdmanpfs.o
	$(CC) -o rdmanpfs $(CFLAGS) ufs.o rdmanpfs.o $(LFLAGS) $(NPFS_LFLAGS) -libverbs -lrdmacm

ramfs: ramfs.o
	$(CC) -o ramfs $(CFLAGS) ramfs.o $(LFLAGS)

nullfs: nullfs.o
	$(CC) -o nullfs $(CFLAGS) nullfs.o $(LFLAGS)

ramfs2: ramfs2.o
	$(CC) -o ramfs2 $(CFLAGS) ramfs2.o $(LFLAGS)

gphotofs.o: gphotofs.c
	$(CC) $(CFLAGS) -I $(GPHOTO_HPATH) -c $*.c

gphotofs: gphotofs.o
	$(CC) -o gphotofs $(CFLAGS) gphotofs.o $(LFLAGS) -L$(GPHOTO_LPATH) -lgphoto2

mboxfs.o: mboxfs.c
	$(CC) $(CFLAGS) -I$(ETPAN_HPATH) -c $*.c

mboxfs: mboxfs.o
	$(CC) -o mboxfs $(CFLAGS) mboxfs.o $(LFLAGS) -L$(ETPAN_LPATH) -letpan -lssl -lcrypto -ldb-4.2 -lsasl2

echofs: echofs.o
	$(CC) -o echofs $(CFLAGS) echofs.o $(LFLAGS)

cpu: cpu.o ../libnpfs/libnpfs.a
	$(CC) -o cpu $(CFLAGS) cpu.o $(LFLAGS)

cpuhelper: cpuhelper.o
	$(CC) -o cpuhelper $(CFLAGS) cpuhelper.o $(LFLAGS)

clean:
	rm -f *.o $(FS) *~

%.c: ../include/npfs.h Makefile

$(FS): ../libnpfs/libnpfs.a

%.o: %.c 
	$(CC) $(CFLAGS) -c $*.c

