SYSNAME:=${shell uname}
SYSNAME!=uname
MULTILIBPATH=${shell test -d /lib64 && echo lib64 || echo lib}
HFILES=../include/npfs.h npfsimpl.h
CFLAGS=-Wall -Werror -g -I../include -D$(SYSNAME)
INCLUDEDIRS:=/usr/include/ /usr/local/include/ /opt/include/

ifeq ($(SYSNAME), SunOS)
	CFLAGS += -D_POSIX_PTHREAD_SEMANTICS
endif

.PHONY: all clean

.all: libnpfs.a

LIBFILES=\
	conn.o\
	error.o\
	fcall.o\
	fdtrans.o\
	fidpool.o\
	file.o\
	fmt.o\
	np.o\
	srv.o\
	trans.o\
	user.o\
# Dropped: uxusers.o socksrv.o pvusers.o

ifeq ($(SYSNAME), Linux)
	LIBFILES += pipesrv.o
	RDMADEPS:=\
		$(and $(foreach dir,$(INCLUDEDIRS),$(wildcard $(dir)/rdma/rdma_cma.h)), \
		$(foreach dir,$(INCLUDEDIRS), $(wildcard $(dir)/infiniband/verbs.h)))
	ifneq ($(strip $(RDMADEPS)),)
		LIBFILES += rdmasrv.o rdmatrans.o
	endif
endif

libnpfs.a: $(LIBFILES)
	ar rc libnpfs.a $(LIBFILES)
	ranlib libnpfs.a

install:
	mkdir -p $(INSTALLPREFIX)/include
	mkdir -p $(INSTALLPREFIX)/$(MULTILIBPATH)
	cp ../include/npfs.h $(INSTALLPREFIX)/include
	cp libnpfs.a $(INSTALLPREFIX)/$(MULTILIBPATH)


clean:
	rm -f *.o *.a *~ core.*

$(LIBFILES): Makefile $(HFILES)

%.o: %.c 
	$(CC) $(CFLAGS) -c $*.c

