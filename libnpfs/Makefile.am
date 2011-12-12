AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS =

noinst_LIBRARIES = libnpfs.a

libnpfs_a_SOURCES = \
	conn.c \
	error.c \
	fcall.c \
	fdtrans.c \
	fidpool.c \
	fmt.c \
	np.c \
	srv.c \
	trans.c \
	user.c \
	npstring.c \
	npfs.h \
	npfsimpl.h \
	9p.h \
	ctl.c \
	xpthread.h

if RDMATRANS
libnpfs_a_SOURCES += rdmatrans.c
endif
