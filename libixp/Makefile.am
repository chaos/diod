AM_CFLAGS = @GCCWARN@
AM_CPPFLAGS =

noinst_LIBRARIES = libixp.a

libixp_a_SOURCES = \
	client.c \
	convert.c \
	error.c \
	ixp.h \
	ixp_local.h \
	ixp_srvutil.h \
	map.c \
	message.c \
	request.c \
	rpc.c \
	server.c \
	socket.c \
	srv_util.c \
	thread.c \
	timer.c \
	transport.c \
	util.c

noinst_PROGRAMS = ixpc

ixpc_LDADD = $(top_builddir)/libixp/libixp.a 
ixpc_SOURCES = ixpc.c
