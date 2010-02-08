SYSNAME:=${shell uname}
SYSNAME!=uname
MULTILIBPATH=${shell test -d /lib64 && echo lib64 || echo lib}
HFILES=../include/npfs.h ../include/npclient.h ../include/npauth.h npaimpl.h
CFLAGS=-Wall -g -I ../include

LIBFILES=\
    help.o\
    msgs.o\
    p9any.o\
    p9sk1.o\
	srv.o\

libnpauth.a: $(LIBFILES)
	ar rc libnpauth.a $(LIBFILES)
	ranlib libnpauth.a

install:
	mkdir -p $(INSTALLPREFIX)/include
	mkdir -p $(INSTALLPREFIX)/$(MULTILIBPATH)
	cp ../include/npauth.h $(INSTALLPREFIX)/include
	cp libnpauth.a $(INSTALLPREFIX)/$(MULTILIBPATH)

clean:
	rm -f *.o *.a *~
	make -C examples clean

$(LIBFILES): Makefile $(HFILES)

%.o: %.c 
	$(CC) $(CFLAGS) -c $*.c
