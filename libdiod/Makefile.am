AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	-I../libnpfs -I../libnpclient -I../liblsd

noinst_LIBRARIES = libdiod.a

libdiod_a_SOURCES = \
	diod_auth.c \
	diod_auth.h \
	diod_log.c \
	diod_log.h \
	diod_conf.c \
	diod_conf.h \
	diod_sock.c \
	diod_sock.h

if RDMATRANS
libdiod_a_SOURCES += diod_rdma.c diod_rdma.h
endif
