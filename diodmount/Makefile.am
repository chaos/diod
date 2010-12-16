AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	-I../libnpfs -I../liblsd -I../libdiod

sbin_PROGRAMS = diodmount

diodmount_LDADD = \
	$(top_builddir)/libdiod/libdiod.a \
	$(top_builddir)/libnpfs/libnpfs.a \
	$(top_builddir)/liblsd/liblsd.a \
	$(LIBWRAP) $(LIBPTHREAD) $(LIBZ) $(LIBLUA) $(LIBMUNGE)

diodmount_SOURCES = \
	diodmount.c \
	opt.c \
	opt.h

man8_MANS = \
        diodmount.8
