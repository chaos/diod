AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	-I$(top_srcdir)/libnpfs \
	-I$(top_srcdir)/libnpclient \
	-I$(top_srcdir)/liblsd

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
