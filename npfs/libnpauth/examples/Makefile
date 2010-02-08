SYSNAME:=${shell uname}
SYSNAME!=uname
CFLAGS=-Wall -g -I ../../include
LFLAGS=-L../../libnpfs -L.. -lnpauth -lnpclient -lnpfs -lpthread -lcrypto

CMD=9ls echofs

all: $(CMD)

9ls: 9ls.o ../../libnpfs/libnpfs.a ../../libnpclient/libnpclient.a ../libnpauth.a
	$(CC) -o $@ $(CFLAGS) 9ls.o $(LFLAGS)

echofs: echofs.o ../../libnpfs/libnpfs.a ../../libnpclient/libnpclient.a ../libnpauth.a
	$(CC) -o $@ $(CFLAGS) echofs.o $(LFLAGS)

clean:
	rm -f *.o *.a *~ $(CMD)

%.o: %.c 
	$(CC) $(CFLAGS) -c $*.c
