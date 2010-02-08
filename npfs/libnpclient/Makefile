SYSNAME:=${shell uname}
SYSNAME!=uname
MULTILIBPATH=${shell test -d /lib64 && echo lib64 || echo lib}
HFILES=../include/npfs.h ../include/npclient.h npcimpl.h
CFLAGS=-Wall -g -I ../include

LIBFILES=\
	dirread.o\
	fid.o\
	fsys.o\
	mount.o\
	netmount.o\
	open.o\
	pool.o\
	read.o\
	remove.o\
	stat.o\
	stat2wstat.o\
	walk.o\
	write.o\

libnpclient.a: $(LIBFILES)
	ar rc libnpclient.a $(LIBFILES)
	ranlib libnpclient.a

install:
	mkdir -p $(INSTALLPREFIX)/include
	mkdir -p $(INSTALLPREFIX)/$(MULTILIBPATH)
	cp ../include/npclient.h $(INSTALLPREFIX)/include
	cp libnpclient.a $(INSTALLPREFIX)/$(MULTILIBPATH)

clean:
	rm -f *.o *.a *~
	make -C examples clean

$(LIBFILES): Makefile $(HFILES)

%.o: %.c 
	$(CC) $(CFLAGS) -c $*.c
