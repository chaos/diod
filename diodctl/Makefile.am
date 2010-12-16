AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	-I../libnpfs -I../liblsd -I../libdiod

sbin_PROGRAMS = diodctl

diodctl_LDADD = \
	$(top_builddir)/libdiod/libdiod.a \
	$(top_builddir)/libnpfs/libnpfs.a \
	$(top_builddir)/liblsd/liblsd.a \
	$(LIBWRAP) $(LIBPTHREAD) $(LIBZ) $(LIBLUA) $(LIBMUNGE)

diodctl_SOURCES = \
	diodctl.c \
	ops.c \
	ops.h \
	serv.c \
	serv.h

man8_MANS = \
        diodctl.8
