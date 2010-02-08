CFLAGS=-Wall -g -I ../../include
LFLAGS=-L../../libnpfs -L.. -lnpauth -lnpclient -lnpfs -lpthread -lcrypto

CMD=authsrv

all: $(CMD)

authsrv: authsrv.o ../../libnpfs/libnpfs.a ../../libnpclient/libnpclient.a ../libnpauth.a
	$(CC) -o $@ $(CFLAGS) authsrv.o $(LFLAGS)

clean:
	rm -f *.o *.a *~ $(CMD)

%.o: %.c 
	$(CC) $(CFLAGS) -c $*.c
