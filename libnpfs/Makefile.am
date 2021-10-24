AM_CFLAGS = @WARNING_CFLAGS@

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

if USE_IMPERSONATION_LINUX
libnpfs_a_SOURCES += user-linux.c
else
if USE_IMPERSONATION_GANESHA
libnpfs_a_SOURCES += user-freebsd.c
else
libnpfs_a_SOURCES += user-stub.c
endif
endif

if RDMATRANS
libnpfs_a_SOURCES += rdmatrans.c
endif
