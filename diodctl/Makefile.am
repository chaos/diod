AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	-I../npfs/include -I../liblsd -I../common

sbin_PROGRAMS = diodctl

diodctl_LDADD = \
	$(top_builddir)/common/libdiod.a \
	$(top_builddir)/npfs/libnpfs/libnpfs.a \
	$(top_builddir)/liblsd/liblsd.a \
	$(LIBWRAP) $(LIBPTHREAD) $(LIBZ) $(LIBLUA) $(LIBMUNGE)

diodctl_SOURCES = \
	diodctl.c \
	ops.c \
	serv.c
